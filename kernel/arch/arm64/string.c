/* ARM64 portable string functions (no x86_64 inline assembly).
 * Replaces kernel/string.c for ARM64 builds — same API, portable C only. */
#include "../kernel/string.h"
#include "../kernel/types.h"
#include "../kernel/kprintf.h"

uintptr_t __stack_chk_guard = 0xDEADBEEFCAFEBABE;

__attribute__((noreturn)) void __stack_chk_fail(void) {
    kprintf("*** STACK SMASHED ***\n");
    for (;;) {
        __asm__ volatile("wfi" ::: "memory");
    }
}

/* ── Memory operations (portable C, no x86 rep prefix) ── */

void *memset(void *ptr, int val, size_t n) {
    if (!ptr || n == 0) return ptr;
    unsigned char *p = (unsigned char *)ptr;
    unsigned char v = (unsigned char)val;
    for (size_t i = 0; i < n; i++) p[i] = v;
    return ptr;
}

void *memcpy(void *dst, const void *src, size_t n) {
    if (!dst || !src || n == 0) return dst;
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    if (!dst || !src || n == 0) return dst;
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if ((uintptr_t)d <= (uintptr_t)s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    return (void *)0;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

/* ── String operations (pure C) ── */

size_t strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    if (!dst || !src) return dst;
    char *orig = dst;
    while ((*dst++ = *src++));
    return orig;
}

char *strcat(char *dst, const char *src) {
    if (!dst || !src) return dst;
    char *orig = dst;
    while (*dst) dst++;
    while ((*dst++ = *src++));
    return orig;
}

char *strchr(const char *s, int c) {
    if (!s) return 0;
    while (*s) { if (*s == c) return (char *)s; s++; }
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

int strncpy_safe(char *dst, const char *src, size_t dstsize) {
    if (!dst || !src || dstsize == 0) return -1;
    size_t i;
    for (i = 0; i < dstsize - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return src[i] ? -1 : 0;
}

int strncat_safe(char *dst, const char *src, size_t dstsize) {
    if (!dst || !src || dstsize == 0) return -1;
    size_t dlen = strlen(dst);
    if (dlen >= dstsize) return -1;
    return strncpy_safe(dst + dlen, src, dstsize - dlen);
}

size_t strlcpy(char *dst, const char *src, size_t dstsize) {
    if (!dst || !src || dstsize == 0) return 0;
    size_t slen = strlen(src);
    size_t to_copy = (slen < dstsize - 1) ? slen : dstsize - 1;
    for (size_t i = 0; i < to_copy; i++) dst[i] = src[i];
    dst[to_copy] = 0;
    return slen;
}

size_t strlcat(char *dst, const char *src, size_t dstsize) {
    if (!dst || !src || dstsize == 0) return 0;
    size_t dlen = strlen(dst);
    if (dlen >= dstsize) return dstsize + strlen(src);
    return dlen + strlcpy(dst + dlen, src, dstsize - dlen);
}

static int tolower_c(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int strcasecmp(const char *a, const char *b) {
    return strncasecmp(a, b, SIZE_MAX);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        int ca = tolower_c((unsigned char)a[i]);
        int cb = tolower_c((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* ── printf-family ── */

static int vsnprintf_internal(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    int n = 0;
    while (*fmt) {
        if (n >= (int)size && size > 0) break;
        if (*fmt == '%') {
            fmt++;
            int pad = 0;
            char ps = ' ';
            if (*fmt == '0') { ps = '0'; fmt++; }
            if (*fmt >= '1' && *fmt <= '9') {
                pad = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    pad = pad * 10 + (*fmt - '0');
                    fmt++;
                }
            }
            if (*fmt == 's') {
                const char *s = __builtin_va_arg(ap, const char *);
                if (!s) s = "(null)";
                int slen = strlen(s);
                int space = (int)size - n;
                if (pad > slen) {
                    for (int i = 0; i < pad - slen; i++) {
                        if (space > 1) buf[n] = ps;
                        n++;
                    }
                }
                for (int i = 0; s[i]; i++) {
                    if (space > 1) buf[n] = s[i];
                    n++;
                }
            } else if (*fmt == 'd' || *fmt == 'i') {
                int v = __builtin_va_arg(ap, int);
                char tmp[16]; int ti = 0;
                int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                if (v == 0) tmp[ti++] = '0';
                while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                if (neg) {
                    if ((int)size - n > 1) buf[n] = '-';
                    n++;
                }
                if (pad > ti) {
                    for (int i = 0; i < pad - ti; i++) {
                        if ((int)size - n > 1) buf[n] = ps;
                        n++;
                    }
                }
                while (ti > 0) {
                    if ((int)size - n > 1) buf[n++] = tmp[--ti];
                    else n++;
                }
            } else if (*fmt == 'u') {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char tmp[16]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                if (pad > ti) {
                    for (int i = 0; i < pad - ti; i++) {
                        if ((int)size - n > 1) buf[n] = ps;
                        n++;
                    }
                }
                while (ti > 0) {
                    if ((int)size - n > 1) buf[n++] = tmp[--ti];
                    else n++;
                }
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char tmp[16]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v) {
                    int d = v % 16;
                    tmp[ti++] = d < 10 ? '0' + d : (*fmt == 'X' ? 'A' : 'a') + d - 10;
                    v /= 16;
                }
                if (pad > ti) {
                    for (int i = 0; i < pad - ti; i++) {
                        if ((int)size - n > 1) buf[n] = ps;
                        n++;
                    }
                }
                while (ti > 0) {
                    if ((int)size - n > 1) buf[n++] = tmp[--ti];
                    else n++;
                }
            } else if (*fmt == 'c') {
                char c = (char)__builtin_va_arg(ap, int);
                if ((int)size - n > 1) buf[n] = c;
                n++;
            } else if (*fmt == '%') {
                if ((int)size - n > 1) buf[n] = '%';
                n++;
            }
            fmt++;
        } else {
            if ((int)size - n > 1) buf[n] = *fmt;
            n++;
            fmt++;
        }
    }
    if (size > 0) {
        if (n < (int)size) buf[n] = 0;
        else buf[size - 1] = 0;
    }
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf_internal(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf_internal(buf, 0x7FFFFFFF, fmt, ap);
    __builtin_va_end(ap);
    return n;
}
