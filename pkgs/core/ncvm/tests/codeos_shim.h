/* codeos_shim.h — run ncvm.c on the host.
 *
 * pkgs/core/ncvm/src/ncvm.c is built for CodeOS and reaches the kernel
 * through kernel/userspace/include/unistd.h, whose sys_* helpers are
 * inline `int $0x80` stubs.  To test that file on the host we put this
 * directory first on the include path so `#include "unistd.h"` finds
 * *this* header instead, and map each syscall onto POSIX.
 *
 * The mapping is deliberately faithful rather than convenient.  The
 * CodeOS open() flags are the Linux x86-64 values (O_WRONLY 0x1, O_RDWR
 * 0x2, O_CREAT 0x40, O_TRUNC 0x200), so they are passed straight to
 * POSIX open().  That matters: a caller that forgets O_CREAT really does
 * fail to create the file here, exactly as it does in the guest.  A shim
 * that quietly created files would have hidden the bug in
 * ncvm.c:write_file().
 *
 * The one thing this file does NOT model is the kernel's readdir record
 * layout -- see sys_readdir below.  It implements the NUL-packed,
 * byte-count contract, which is what ncvm.c and crosvm-launcher.c already
 * assumed; the kernel side of that contract is verified by booting
 * (`ncvm --selftest` plants 8 names and requires all 8 back), not here.
 */
#ifndef CODEOS_SHIM_H
#define CODEOS_SHIM_H

/* -std=c11 hides the POSIX declarations unless this is set.  The guest
 * code sees them unconditionally via the kernel's own headers. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ── constants ncvm.c expects from unistd.h ──────────────────────────── */
#define KILL_SIGTERM  15
#define KILL_SIGKILL  9
#define KILL_SIGSTOP  19
#define KILL_SIGCONT  18
#define PERSONALITY_LINUX 1
#define PERSONALITY_CODEOS 0

/* Linux x86-64 values, identical to the kernel's (unistd.h). */
#ifndef O_WRONLY
#define O_WRONLY  0x1
#endif
#ifndef O_RDWR
#define O_RDWR    0x2
#endif
#ifndef O_CREAT
#define O_CREAT   0x40
#endif
#ifndef O_TRUNC
#define O_TRUNC   0x200
#endif
#ifndef O_APPEND
#define O_APPEND  0x400
#endif

/* ── test observability ────────────────────────────────────────────────
 * sys_write() in the guest writes to stdout, never to a file.  Tests
 * assert on console_bytes() so a write that was supposed to land in a
 * file but went to the console shows up as a failure instead of as
 * stray terminal noise.
 */
static char g_console[65536];
static int  g_console_len;

/* Test observability.  Marked unused because not every test needs them and
 * -Werror is on. */
static void console_reset(void) __attribute__((unused));
static const char *console_text(void) __attribute__((unused));
static int console_len(void) __attribute__((unused));
static int console_has(const char *s) __attribute__((unused));
static void shim_printf(const char *fmt, ...) __attribute__((unused));

static void console_reset(void) { g_console_len = 0; g_console[0] = 0; }
static const char *console_text(void) { return g_console; }
static int console_len(void) { return g_console_len; }
static int console_has(const char *s) { return strstr(g_console, s) != NULL; }

/* ── the syscalls ncvm.c uses ─────────────────────────────────────────── */
static inline int sys_open(const char *path, int flags) {
    /* O_APPEND/O_TRUNC in CodeOS act on the fd's own offset, not the
     * file, so drop O_TRUNC here and let the caller see a faithful
     * "opened but not truncated" file.  Keep O_CREAT. */
    return open(path, flags & ~(O_TRUNC | O_APPEND), 0644);
}

static inline int sys_read(int fd, void *buf, int count) {
    return (int)read(fd, buf, (size_t)count);
}

static inline int sys_pwrite(int fd, const void *buf, int count) {
    return (int)write(fd, buf, (size_t)count);
}

static inline int sys_write(const void *buf, int count) {
    /* stdout in the guest; captured here. */
    if (g_console_len + count > (int)sizeof(g_console) - 1) count = (int)sizeof(g_console) - 1 - g_console_len;
    memcpy(g_console + g_console_len, buf, (size_t)count);
    g_console_len += count;
    g_console[g_console_len] = 0;
    return count;
}

static inline int sys_close(int fd) { return close(fd); }
static inline int sys_unlink(const char *path) { return unlink(path); }
static inline int sys_getpid(void) { return (int)getpid(); }
static inline int sys_set_personality(int p) { (void)p; return 0; }

/* CodeOS sys_mkdir has no mode argument and, like POSIX mkdir, fails if the
 * directory already exists -- syscall.c:sys_mkdir returns -1 whenever
 * fs_mkdir() does.  Deliberately not softened to "EEXIST is success": the
 * point of this shim is the contract, and a caller that mishandles the
 * failure has to fail here too. */
static inline int sys_mkdir(const char *path) {
    return mkdir(path, 0755) == 0 ? 0 : -1;
}

static inline void sys_exit(int status) { _exit(status); }

static inline void sys_sleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static inline int sys_fork(void) {
    fflush(stdout);
    return (int)fork();
}

static inline int sys_wait(int pid, int *status) {
    int st = 0;
    if (waitpid((pid_t)pid, &st, 0) < 0) return -1;
    if (status) *status = st;
    return pid;
}

static inline int sys_kill(int pid, int sig) {
    /* CodeOS signal numbers match Linux for the ones ncvm.c uses. */
    return kill((pid_t)pid, sig);
}

static inline int sys_execve(const char *path, char **argv, int argc) {
    (void)argc;               /* execv reads the NUL terminator itself */
    fflush(stdout);
    execv(path, argv);
    return -1;
}

/* ncvm.c reports through printf(); route it into the same console buffer so
 * a test can assert on "ncvm: ..." lines instead of scraping the terminal. */
static void shim_printf(const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    sys_write(tmp, n);
}

/* NUL-packed, byte-count contract -- what ncvm.c already assumes. */
static inline int sys_readdir(const char *path, char *names, int max_bytes) {
    if (max_bytes <= 0) return -1;
    DIR *d = opendir(path);
    if (!d) return -1;
    int total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        int len = (int)strlen(e->d_name) + 1;
        if (total + len > max_bytes) break;
        memcpy(names + total, e->d_name, (size_t)len);
        total += len;
    }
    closedir(d);
    return total;
}

#endif /* CODEOS_SHIM_H */
