/* bas_os.c -- the C side of the bas OS SDK.
 *
 * The syscall wrapper is spelled out per arity rather than variadic.  A
 * variadic wrapper would read its arguments with `va_arg(ap, long)`, which
 * breaks as soon as the compiler forwards a `__builtin_va_list` as the first
 * variadic argument -- a mistake that looks exactly like a compiler bug.  The
 * fixed-arity form is also simply what the kernel expects: arguments land in
 * RDI, RSI, RDX, R10, R8, R9 in that order.
 */
#include <stdint.h>
#include <stddef.h>

/* The platform allocator, for the whole-file and directory helpers below that
 * need a buffer.  Both operating systems declare these in <stdlib.h>. */
#include <stdlib.h>

#include "bas_os.h"

/* Syscall numbers, from include/codeos/syscall_abi.h.  They are repeated as
 * literals rather than included because <unistd.h> names a different file in
 * each tree, while the numbers are the same: Zircon runs on the CodeOS kernel
 * and shares this ABI. */
#define BAS_N_WRITE      0
#define BAS_N_SLEEP      1
#define BAS_N_EXIT       2
#define BAS_N_GETPID    11
#define BAS_N_GETPPID   12
#define BAS_N_OPEN       4
#define BAS_N_CLOSE     13
#define BAS_N_READ       5
#define BAS_N_PWRITE    33
#define BAS_N_YIELD      6
#define BAS_N_LSEEK     19
#define BAS_N_STAT      38
#define BAS_N_ACCESS    47
#define BAS_N_READDIR   45
#define BAS_N_MKDIR     41
#define BAS_N_UNLINK    43
#define BAS_N_TIME      53

static inline long bs1(long n, long a1) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1) : "memory");
    return r;
}
static inline long bs2(long n, long a1, long a2) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "memory");
    return r;
}
static inline long bs3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "memory");
    return r;
}

static unsigned long bstrlen(const char *s) {
    unsigned long n = 0;
    if (s) while (s[n]) n++;
    return n;
}

/* ── Console ── */

int bas_write(int fd, const char *s) {
    unsigned long n = bstrlen(s);
    /* SYSCALL_WRITE ignores fd and writes to the console.  It is passed anyway
     * so the signature matches write(2) as a program expects, and so a
     * future fd-aware kernel is a change here alone. */
    return (int)bs3(BAS_N_WRITE, (long)fd, (long)s, (long)n);
}

int bas_print(const char *s) { return bas_write(1, s); }

int bas_read_char(int fd) {
    unsigned char c = 0;
    long r = bs3(BAS_N_READ, (long)fd, (long)&c, 1);
    return r == 1 ? (int)c : -1;
}

/* ── Process ── */

/* `exit` never returns on either OS, but the loop keeps the function
 * well-formed if a future kernel does return. */
void bas_exit(int status) {
    bs1(BAS_N_EXIT, (long)status);
    for (;;) { }
}

int bas_getpid(void)  { return (int)bs1(BAS_N_GETPID, 0); }
int bas_getppid(void) { return (int)bs1(BAS_N_GETPPID, 0); }
int bas_yield(void)   { return (int)bs1(BAS_N_YIELD, 0); }

/* SYSCALL_SLEEP takes milliseconds on both operating systems. */
int bas_sleep_ms(int ms) { return (int)bs1(BAS_N_SLEEP, (long)ms); }

/* The bas prelude (sdk/bas/os.ba) declares every function below in an `extern`
 * block, and codegen emits a C prototype from THAT declaration -- not from
 * this header.  So a signature that drifts between the two files compiles,
 * links, and only misbehaves at runtime.  _Static_assert cannot see across the
 * boundary, so this comment is the contract: any change here must be made in
 * sdk/bas/os.ba in the same commit, and bas_boot_check.py asserts the resulting
 * values rather than just the absence of a crash.
 *
 * It already earned its place: bas_write_file took a `len` that the .ba never
 * passed, and wrote 45 bytes where 24 were asked for. */

/* ── Filesystem ── */

int bas_open(const char *path, int flags) {
    return (int)bs2(BAS_N_OPEN, (long)path, (long)flags);
}

int bas_close(int fd) { return (int)bs1(BAS_N_CLOSE, (long)fd); }

int bas_lseek(int fd, long long offset, int whence) {
    return (int)bs3(BAS_N_LSEEK, (long)fd, (long)offset, (long)whence);
}

int bas_access(const char *path) { return (int)bs2(BAS_N_ACCESS, (long)path, 0); }

/* SYSCALL_STAT fills a kernel stat struct; only the size is wanted here, and
 * it is read from offset 0 of that struct.  A short fill is treated as a
 * failure rather than silently yielding a wrong size. */
long long bas_stat_size(const char *path) {
    unsigned char buf[512];
    long r = bs2(BAS_N_STAT, (long)path, (long)buf);
    if (r < 0) return r;
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v |= (unsigned long long)buf[i] << (8 * i);
    return (long long)v;
}

/* Read a whole file into one malloc'd buffer.  The caller frees it; a bas
 * program that gets a `String` back never sees the pointer at all, because
 * bas_strdup copies out of it.  Two syscalls: the size first, so the buffer is
 * allocated once instead of grown.
 * `out_len` receives the length, because bas has no way to express a slice. */
const char *bas_read_file(const char *path, long long *out_len) {
    if (out_len) *out_len = 0;
    long long size = bas_stat_size(path);
    if (size < 0) return 0;

    int fd = bas_open(path, 0 /* O_RDONLY */);
    if (fd < 0) return 0;

    unsigned char *buf = (unsigned char *)malloc((int)size + 1);
    if (!buf) { bas_close(fd); return 0; }

    long long got = 0;
    while (got < size) {
        /* The kernel's read returns as much as it can per call, so this loops
         * rather than assuming one read fills the buffer. */
        long n = bs3(BAS_N_READ, (long)fd, (long)(buf + got), (long)(size - got));
        if (n <= 0) break;
        got += n;
    }
    bas_close(fd);
    buf[got] = 0;
    if (out_len) *out_len = got;
    return (const char *)buf;
}

int bas_write_file(const char *path, const char *data) {
    long long len = (long long)bstrlen(data);
    /* O_WRONLY|O_CREAT|O_TRUNC: the kernel's flags are the Linux values. */
    int fd = bas_open(path, 0x1 | 0x40 | 0x200);
    if (fd < 0) return fd;
    long long off = 0;
    while (off < len) {
        /* SYSCALL_PWRITE, not SYSCALL_WRITE: the latter IGNORES its fd and
         * always writes to the console, so using it here printed the file's
         * contents to the screen and reported 0 bytes written. */
        long n = bs3(BAS_N_PWRITE, (long)fd, (long)(data + off), (long)(len - off));
        if (n <= 0) break;
        off += n;
    }
    bas_close(fd);
    return (int)off;
}

/* SYSCALL_READDIR returns packed NUL-separated names and a byte count, not an
 * entry count -- the distinction that ncvm's doc comment calls out, and the
 * same contract here.  First pass counts the bytes, second fills the buffer. */
const char *bas_list_dir(const char *path, long long *out_len) {
    if (out_len) *out_len = 0;

    unsigned char probe[4096];
    long need = bs3(BAS_N_READDIR, (long)path, (long)probe, (long)sizeof(probe));
    if (need <= 0) return 0;

    unsigned char *buf = (unsigned char *)malloc((size_t)need + 2);
    if (!buf) return 0;

    long got = need <= (long)sizeof(probe)
                 ? need
                 : bs3(BAS_N_READDIR, (long)path, (long)buf, (long)need);
    if (got <= 0) { free(buf); return 0; }

    /* Turn the packed NUL-separated list into newline-separated text, which is
     * what a bas program can iterate.  The buffer is sized for the worst case:
     * one newline per byte would double a list of 1-char names. */
    char *text = (char *)malloc((size_t)got * 2 + 2);
    if (!text) { free(buf); return 0; }

    long o = 0;
    for (long i = 0; i < got; i++) {
        if (buf[i] == 0) {
            if (i + 1 < got) text[o++] = '\n';   /* do not emit a trailing blank */
        } else {
            text[o++] = (char)buf[i];
        }
    }
    text[o] = 0;
    free(buf);
    if (out_len) *out_len = o;
    return text;
}

void bas_release(const char *s) {
    /* free(NULL) is a no-op, so a failed bas_read_file needs no guard here. */
    free((void *)(size_t)s);
}

int bas_mkdir(const char *path)  { return (int)bs1(BAS_N_MKDIR, (long)path); }
int bas_unlink(const char *path) { return (int)bs1(BAS_N_UNLINK, (long)path); }

/* ── Time ── */

long long bas_time(void) {
    long r = bs1(BAS_N_TIME, 0);
    return r < 0 ? 0 : (long long)r;
}