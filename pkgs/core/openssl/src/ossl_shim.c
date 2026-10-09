/*
 * ossl_shim.c -- freestanding libc + POSIX shim for OpenSSL linked into the
 * CodeOS kernel.
 *
 * Upstream OpenSSL is written against hosted libc/POSIX and a BSD socket
 * API.  The kernel has neither.  This file supplies the small, finite set of
 * symbols OpenSSL's TLS client actually requires, mapping each onto either an
 * existing kernel primitive (in kprintf.h, string.h, timer.h) or a minimal
 * local implementation.  Everything here resolves at kernel link time with no
 * glibc/musl dependency.
 *
 * Symbols are declared WEAK so that, when the kernel already ships a strong
 * definition (e.g. kernel/cxx_support.cpp provides its own abort, printf,
 * strtol, stdio and POSIX surface), the kernel's version wins and this file's
 * fallback is discarded.  OpenSSL's strong references are still satisfied
 * either way, so no duplicate-symbol link errors occur.
 *
 * Symbols the kernel ALREADY provides as strong definitions (and which we
 * therefore never define here, weak or otherwise):
 *   malloc / calloc / realloc / free         (mm.c)
 *   strlen strcmp strncmp strcpy strcat strchr strstr
 *   memcpy memset memmove memcmp memchr
 *   strdup strcasecmp strncasecmp snprintf   (string.c)
 */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "kprintf.h"
#include "timer.h"
#include "rng.h"

#define WEAK __attribute__((weak))

/* ── errno: OpenSSL references errno constants (ENOMEM, EINVAL ...) and
 *    sets the TLS-PROVIDER error via ossl_err_set in some failure paths.
 *    We provide a single per-CPU-free thread-local slot and the POSIX
 *    accessor required by libc consumers; the kernel never reads it back via
 *    <errno.h>, so a plain global is sufficient and safe (no threads here). */
int ossl_errno_slot = 0;
WEAK int *__errno_location(void) { return &ossl_errno_slot; }
WEAK int *__error(void)          { return &ossl_errno_slot; }  /* macOS alias */

#define ENOMEM 12
#define EINVAL 22
#define EAGAIN 11

/* ── abort / exit ───────────────────────────────────────────────
 * The kernel cannot unwind or raise a signal; on a software fault the safest
 * action OpenSSL can take is to log and halt.  Redirecting to a kernel panic
 * would be hostile mid-networking, so we log and (conservatively) spin --
 * OpenSSL only calls abort() on truly unrecoverable internal errors. */
WEAK void abort(void) {
    kprintf("https[ossl]: abort() called -- unrecoverable OpenSSL error\n");
    for (;;) ;
}
WEAK void exit(int status)         { (void)status; abort(); }
WEAK void _exit(int status)        { (void)status; abort(); }

/* ── time ───────────────────────────────────────────────────────
 * Kernel has no absolute wall clock; we expose monotonic ms as seconds so
 * OpenSSL's time-based logic (session tickets, OCSP, cert date sanity) stays
 * structurally sound without a real epoch.  Verify() skips validity-date
 * checks for the same reason mbedtls does (no MBEDTLS_HAVE_TIME_DATE). */
WEAK time_t time(time_t *tloc) {
    time_t s = (time_t)(timer_get_milliseconds() / 1000u);
    if (tloc) *tloc = s;
    return s;
}

/* ── getenv ────────────────────────────────────────────────────
 * OpenSSL consults a handful of env vars; a NULL return means "unset" which
 * every consumer already treats as "use defaults". */
WEAK char *getenv(const char *name) {
    (void)name;
    return NULL;
}

/* ── qsort ─────────────────────────────────────────────────────
 * Mergesort; avoids the pivoting pathologies of quicksort and needs no
 * stack-growing recursion.  OpenSSL uses qsort in OCSP/stack sorting only
 * on paths the TLS client does not normally take, but supply it to be safe. */
static void msort(char *base, char *tmp, size_t n, size_t sz,
                  int (*cmp)(const void *, const void *)) {
    if (n < 2) return;
    size_t m = n / 2;
    char *lo = base, *hi = base + m * sz;
    msort(lo, tmp, m, sz, cmp);
    msort(hi, tmp, n - m, sz, cmp);

    size_t i = 0, j = 0, k = 0;
    while (i < m && j < (n - m)) {
        char *a = lo + i * sz, *b = hi + j * sz;
        if (cmp(a, b) <= 0) { __builtin_memcpy(tmp + k++ * sz, a, sz); i++; }
        else                { __builtin_memcpy(tmp + k++ * sz, b, sz); j++; }
    }
    while (i < m)       { __builtin_memcpy(tmp + k++ * sz, lo + i++ * sz, sz); }
    while (j < n - m)   { __builtin_memcpy(tmp + k++ * sz, hi + j++ * sz, sz); }
    __builtin_memcpy(base, tmp, n * sz);
}

WEAK void qsort(void *base, size_t nmemb, size_t size,
                int (*compar)(const void *, const void *)) {
    if (nmemb < 2 || size == 0) return;
    void *tmp = malloc(nmemb * size);
    if (!tmp) { abort(); return; }
    msort((char *)base, (char *)tmp, nmemb, size, compar);
    free(tmp);
}

/* ── string pieces OpenSSL wants that the kernel string.c omits ── */
WEAK char *strtok(char *str, const char *delim) {
    static char *save = NULL;
    if (str) save = str;
    if (!save) return NULL;

    char *start = save;
    while (*start && strchr(delim, (unsigned char)*start)) start++;
    if (!*start) { save = NULL; return NULL; }

    char *end = start;
    while (*end && !strchr(delim, (unsigned char)*end)) end++;
    if (*end) { *end = '\0'; save = end + 1; }
    else      { save = NULL; }
    return start;
}

WEAK char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

WEAK char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) d++;
    size_t i = 0;
    while (i < n && src[i]) { d[i] = src[i]; i++; }
    d[i] = '\0';
    return dst;
}

WEAK size_t strnlen(const char *s, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

WEAK int vsnprintf(char *s, size_t n, const char *fmt, va_list ap); /* string.h impl */

/* ── stdio surface OpenSSL compiles against ─────────────────────
 * The memory-BIO client never touches real files or stdio streams, but the
 * linked objects reference these symbols (PEM_read_fp, ERR_print_errors_fp
 * and friends are pulled in by table-driven code even when unused at warm
 * runtime).  We provide inert implementations so the kernel links cleanly
 * and, if ever called, degrades gracefully instead of faulting. */

WEAK int fprintf(FILE *stream, const char *fmt, ...) { (void)stream; (void)fmt; return 0; }
WEAK FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
WEAK int fclose(FILE *stream) { (void)stream; return 0; }
WEAK size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr; (void)size; (void)nmemb; (void)stream; return 0;
}
WEAK size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr; (void)size; (void)nmemb; (void)stream; return 0;
}
WEAK int fseek(FILE *stream, long offset, int whence) {
    (void)stream; (void)offset; (void)whence; return -1;
}
WEAK long ftell(FILE *stream) { (void)stream; return -1; }
WEAK int fgetc(FILE *stream)            { (void)stream; return -1; }
WEAK int fputc(int c, FILE *stream)     { (void)c; (void)stream; return -1; }
WEAK int fflush(FILE *stream)           { (void)stream; return 0; }
WEAK int fputs(const char *s, FILE *stream) { (void)s; (void)stream; return -1; }
WEAK int puts(const char *s)            { (void)s; return 0; }
WEAK int printf(const char *fmt, ...)   { (void)fmt; return 0; }
WEAK int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    (void)stream; (void)fmt; (void)ap; return 0;
}
WEAK int vprintf(const char *fmt, va_list ap) { (void)fmt; (void)ap; return 0; }

/* ── POSIX misc OpenSSL refs ────────────────────────────────────
 * These are referenced by libcrypto's platform layer even though the
 * freestanding TLS client exercises none of them at runtime. */
WEAK int uname(void *buf) { (void)buf; return -1; }
WEAK long sysconf(int name) { (void)name; return -1; }
WEAK int signal(int signum, void (*handler)(int)) { (void)signum; (void)handler; return 0; }

/* ════════════════════════════════════════════════════════════════
 * Additional libc / POSIX surface OpenSSL's linked objects reference.
 * The memory-BIO TLS client drives none of these at runtime, but the
 * monolithic OpenSSL objects reference them (socket layer, DSO loader,
 * directory scan, ctype tables, C23 scanf/strtol aliases).  Supplying a
 * minimal, fail-safe version lets the kernel link the genuine archives as-is.
 * ════════════════════════════════════════════════════════════════ */

/* Kernel already provides malloc/calloc/realloc/free, strlen/strcmp/strncmp/
 * strcpy/strcat/strchr/strstr/memset/memcpy/memmove/memcmp/memchr/strdup/
 * strcasecmp/strncasecmp/snprintf/strlcpy/strlcat -- do NOT redefine here. */

/* ── string extras missing from kernel string.c ──────────────── */
WEAK char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (const char *p = s; *p; p++) if ((unsigned char)*p == (unsigned char)c) last = p;
    if ((unsigned char)c == 0) return (char *)(s + strlen(s));
    return (char *)last;
}
WEAK size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n] && !strchr(reject, (unsigned char)s[n])) n++;
    return n;
}
WEAK size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n] && strchr(accept, (unsigned char)s[n])) n++;
    return n;
}
WEAK char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) if (strchr(accept, (unsigned char)*s)) return (char *)s;
    return NULL;
}

/* ── bespoke strtol/strtoul (real implementations, not inert) ── */
WEAK long int strtol(const char *nptr, char **endptr, int base) {
    const char *p = nptr; while (*p == ' ' || *p == '\t') p++;
    int sign = 1; if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
    unsigned long acc = 0, cutoff = 1UL << 31;
    int any = 0;
    if (base == 0) {
        if (*p == '0') { base = (p[1] == 'x' || p[1] == 'X') ? 16 : 8; if (base == 16) p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (;; p++) {
        int dig; char c = *p;
        if (c >= '0' && c <= '9') dig = c - '0';
        else if (c >= 'a' && c <= 'z') dig = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') dig = c - 'A' + 10;
        else break;
        if (dig >= base) break;
        if (acc > (cutoff - dig) / (unsigned)base) any = -1;
        else { acc = acc * (unsigned)base + dig; any = 1; }
    }
    if (endptr) *endptr = (char *)(any ? p : nptr);
    if (any < 0) return sign < 0 ? (-1L << 31) : 0x7fffffffL;
    return sign < 0 ? -(long)acc : (long)acc;
}
WEAK unsigned long int strtoul(const char *nptr, char **endptr, int base) {
    const char *p = nptr; while (*p == ' ' || *p == '\t') p++;
    if (*p == '+') p++;
    unsigned long acc = 0; int any = 0;
    if (base == 0) {
        if (*p == '0') { base = (p[1] == 'x' || p[1] == 'X') ? 16 : 8; if (base == 16) p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (;; p++) {
        int dig; char c = *p;
        if (c >= '0' && c <= '9') dig = c - '0';
        else if (c >= 'a' && c <= 'z') dig = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') dig = c - 'A' + 10;
        else break;
        if (dig >= base) break;
        acc = acc * (unsigned)base + (unsigned)dig; any = 1;
    }
    if (endptr) *endptr = (char *)(any ? p : nptr);
    return acc;
}
/* glibc 2.38+ / C23 aliases OpenSSL was compiled against */
WEAK long int __isoc23_strtol(const char *n, char **e, int b) { return strtol(n, e, b); }
WEAK unsigned long int __isoc23_strtoul(const char *n, char **e, int b) { return strtoul(n, e, b); }
WEAK int __isoc23_sscanf(const char *s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }

/* ── ctype low-level glibc tables ───────────────────────────── */
static const unsigned short s_ctype_b[257];
static const unsigned short *s_ctype_b_p = s_ctype_b;
WEAK const unsigned short **__ctype_b_loc(void) { return &s_ctype_b_p; }
static const int s_ctype_tolower[257];
static const int *s_ctype_tolower_p = s_ctype_tolower;
WEAK const int **__ctype_tolower_loc(void) { return &s_ctype_tolower_p; }
static const int s_ctype_toupper[257];
static const int *s_ctype_toupper_p = s_ctype_toupper;
WEAK const int **__ctype_toupper_loc(void) { return &s_ctype_toupper_p; }

/* ── stdio streams + extra stdio entries ────────────────────── */
static struct _IO_FILE s_stdin = {0};
static struct _IO_FILE s_stdout = {0};
static struct _IO_FILE s_stderr = {0};
WEAK FILE *stdin  = &s_stdin;
WEAK FILE *stdout = &s_stdout;
WEAK FILE *stderr = &s_stderr;
WEAK char *fgets(char *s, int n, FILE *stream) { (void)s; (void)n; (void)stream; return NULL; }
WEAK int feof(FILE *stream) { (void)stream; return 1; }
WEAK int ferror(FILE *stream) { (void)stream; return 1; }
WEAK int fileno(FILE *stream) { (void)stream; return -1; }
WEAK void perror(const char *s) { (void)s; }
WEAK int atexit(void (*func)(void)) { (void)func; return 0; }
WEAK int posix_memalign(void **memptr, size_t alignment, size_t size) {
    (void)alignment;
    void *p = malloc(size ? size : 1);
    if (!p) return 12;
    *memptr = p;
    return 0;
}
WEAK char *secure_getenv(const char *name) { (void)name; return NULL; }

/* ── time functions ─────────────────────────────────────────── */
WEAK int gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return -1; }
WEAK int clock_gettime(int clk, struct timespec *ts) {
    (void)clk;
    if (ts) { ts->tv_sec = (long)(timer_get_milliseconds() / 1000u); ts->tv_nsec = 0; }
    return 0;
}
WEAK int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req) timer_sleep((uint32_t)((req->tv_sec * 1000) + (req->tv_nsec / 1000000)));
    return 0;
}
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year,
            tm_wday, tm_yday, tm_isdst; };
static struct tm s_gmtime;
WEAK struct tm *gmtime(const time_t *t) {
    (void)t;
    s_gmtime.tm_year = 0; s_gmtime.tm_mon = 0; s_gmtime.tm_mday = 1;
    s_gmtime.tm_hour = 0; s_gmtime.tm_min = 0; s_gmtime.tm_sec = 0;
    s_gmtime.tm_isdst = 0; s_gmtime.tm_wday = 0; s_gmtime.tm_yday = 0;
    return &s_gmtime;
}
WEAK struct tm *gmtime_r(const time_t *t, struct tm *r) {
    struct tm *g = gmtime(t); if (r) *r = *g; return r;
}
WEAK struct tm *localtime(const time_t *t) { return gmtime(t); }
WEAK time_t mktime(struct tm *tm) { (void)tm; return 0; }

/* ── basic POSIX I/O ─────────────────────────────────────────────
 * OpenSSL's OS entropy source (rand_unix.c) seeds its DRBG from
 * /dev/urandom (or /dev/random).  There is no such device in the kernel
 * VFS, so we back it with the kernel RNG (rng_next) through a reserved
 * mock fd.  This is the only path OpenSSL's SSL layer needs from the
 * BSD/POSIX file layer; everything else stays inert.
 *
 * Mock fd rules:
 *   open("/dev/urandom"|"/dev/random", ...)  -> URAND_FD (3)
 *   read(URAND_FD, buf, n)                   -> fill n RNG bytes
 *   close(URAND_FD) / lseek / fstat / stat   -> succeed harmlessly
 */
#define URAND_FD 3

WEAK int read(int fd, void *buf, unsigned long count) {
    if (fd == URAND_FD && buf && count > 0) {
        unsigned char *b = (unsigned char *)buf;
        for (unsigned long i = 0; i < count; i++)
            b[i] = (unsigned char)(rng_next() & 0xff);
        return (int)count;
    }
    return -1;
}
WEAK int write(int fd, const void *buf, unsigned long count) {
    (void)fd; (void)buf; (void)count; return -1;
}
WEAK int close(int fd) { (void)fd; return 0; }
WEAK int open(const char *path, int flags, ...) {
    (void)flags;
    if (path && (strcmp(path, "/dev/urandom") == 0 ||
                 strcmp(path, "/dev/random") == 0))
        return URAND_FD;
    return -1;
}
WEAK long lseek(int fd, long offset, int whence) {
    (void)fd; (void)offset; (void)whence; return 0;
}
WEAK int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
WEAK int ioctl(int fd, unsigned long req, ...) { (void)fd; (void)req; return -1; }
WEAK int poll(void *fds, unsigned long nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
WEAK int stat(const char *p, void *sb) {
    (void)sb;
    if (p && (strcmp(p, "/dev/urandom") == 0 ||
              strcmp(p, "/dev/random") == 0))
        return 0;
    return -1;
}
WEAK int lstat(const char *p, void *sb) {
    (void)sb;
    if (p && (strcmp(p, "/dev/urandom") == 0 ||
              strcmp(p, "/dev/random") == 0))
        return 0;
    return -1;
}
WEAK int fstat(int fd, void *sb) { (void)fd; (void)sb; return 0; }
WEAK int getpid(void) { return 0; }
WEAK long syscall(long n, ...) { (void)n; return -1; }
WEAK int tcgetattr(int fd, void *t) { (void)fd; (void)t; return -1; }
WEAK int tcsetattr(int fd, int a, const void *t) { (void)fd; (void)a; (void)t; return -1; }
WEAK int isatty(int fd) { (void)fd; return 0; }

/* ── socket layer (never used by the memory-BIO client) ─────── */
WEAK int socket(int d, int t, int p)          { (void)d; (void)t; (void)p; return -1; }
WEAK int bind(int fd, const void *a, unsigned l) { (void)fd; (void)a; (void)l; return -1; }
WEAK int connect(int fd, const void *a, unsigned l) { (void)fd; (void)a; (void)l; return -1; }
WEAK int listen(int fd, int b)                { (void)fd; (void)b; return -1; }
WEAK int accept(int fd, void *a, void *l)     { (void)fd; (void)a; (void)l; return -1; }
WEAK int shutdown(int fd, int h)              { (void)fd; (void)h; return -1; }
WEAK int getsockopt(int fd, int l, int o, void *v, void *n) { (void)fd; (void)l; (void)o; (void)v; (void)n; return -1; }
WEAK int setsockopt(int fd, int l, int o, const void *v, unsigned n) { (void)fd; (void)l; (void)o; (void)v; (void)n; return -1; }
WEAK int getsockname(int fd, void *a, void *l) { (void)fd; (void)a; (void)l; return -1; }
WEAK int getpeername(int fd, void *a, void *l) { (void)fd; (void)a; (void)l; return -1; }
WEAK int socketpair(int d, int t, int p, int *sv) { (void)d; (void)t; (void)p; (void)sv; return -1; }
WEAK long sendto(int fd, const void *b, unsigned long n, int f, const void *a, unsigned l) { (void)fd; (void)b; (void)n; (void)f; (void)a; (void)l; return -1; }
WEAK long recvfrom(int fd, void *b, unsigned long n, int f, void *a, void *l) { (void)fd; (void)b; (void)n; (void)f; (void)a; (void)l; return -1; }
WEAK long sendmsg(int fd, const void *m, int f) { (void)fd; (void)m; (void)f; return -1; }
WEAK long recvmsg(int fd, void *m, int f)       { (void)fd; (void)m; (void)f; return -1; }
WEAK long sendmmsg(int fd, void *m, unsigned v, int f) { (void)fd; (void)m; (void)v; (void)f; return -1; }
WEAK long recvmmsg(int fd, void *m, unsigned v, int f, void *t) { (void)fd; (void)m; (void)v; (void)f; (void)t; return -1; }
WEAK int getaddrinfo(const char *n, const char *s, const void *h, void **r) { (void)n; (void)s; (void)h; (void)r; return 1; }
WEAK void freeaddrinfo(void *r) { (void)r; }
WEAK int getnameinfo(const void *a, unsigned l, char *h, unsigned hl, char *s, unsigned sl, int f) { (void)a; (void)l; (void)h; (void)hl; (void)s; (void)sl; (void)f; return 1; }
WEAK const char *gai_strerror(int e) { (void)e; return "gai"; }
WEAK void *gethostbyname(const char *n) { (void)n; return NULL; }

/* ── shared-object loader (inert) ───────────────────────────── */
WEAK void *dlopen(const char *f, int m) { (void)f; (void)m; return NULL; }
WEAK int dlclose(void *h) { (void)h; return -1; }
WEAK void *dlsym(void *h, const char *s) { (void)h; (void)s; return NULL; }
WEAK int dladdr(const void *a, void *i) { (void)a; (void)i; return 0; }
WEAK char *dlerror(void) { return "dlerror"; }

/* ── memory mapping (inert) ─────────────────────────────────── */
WEAK void *mmap(void *a, unsigned long l, int p, int f, int fd, long o) { (void)a; (void)l; (void)p; (void)f; (void)fd; (void)o; return (void *)-1; }
WEAK int munmap(void *a, unsigned long l) { (void)a; (void)l; return -1; }
WEAK int mprotect(void *a, unsigned long l, int p) { (void)a; (void)l; (void)p; return -1; }
WEAK int madvise(void *a, unsigned long l, int adv) { (void)a; (void)l; (void)adv; return -1; }
WEAK int mlock(const void *a, unsigned long l) { (void)a; (void)l; return -1; }

/* ── directory scan (inert) ─────────────────────────────────── */
WEAK void *opendir(const char *p) { (void)p; return NULL; }
WEAK void *readdir(void *d) { (void)d; return NULL; }
WEAK int closedir(void *d) { (void)d; return -1; }

/* ── signals ────────────────────────────────────────────────── */
WEAK int sigaction(int s, const void *a, void *o) { (void)s; (void)a; (void)o; return -1; }

/* ── strerror family ────────────────────────────────────────── */
WEAK char *strerror(int e) { (void)e; return "strerror"; }
WEAK int __xpg_strerror_r(int e, char *b, size_t n) { (void)e; if (n) b[0] = 0; return 0; }
WEAK int strerror_r(int e, char *b, size_t n) { return __xpg_strerror_r(e, b, n); }
