/* Shared host shim for the gzip and tar ports.
 *
 * Both ports are written against the CodeOS userspace syscall API (see
 * pkgs/core/shell/src/shell.c for the measured contract):
 *
 *   sys_open(path, flags)          flags are the Linux x86-64 values
 *   sys_read(fd, buf, n)
 *   sys_pwrite(fd, buf, n)         writes to a file fd (sys_write is stdout-only)
 *   sys_write(buf, n)              stdout
 *   sys_close(fd), sys_unlink(path)
 *   sys_mkdir(path)                one argument, no mode
 *   sys_readdir(path, buf, cap)    NUL-packed names, returns a byte count
 *
 * The shim maps those onto POSIX 1:1 so the *real* port sources can be built
 * and exercised on the build machine. In particular O_TRUNC is deliberately
 * NOT emulated: on CodeOS it acts on the fd's own offset, not on the file,
 * which ncvm's shim (pkgs/core/ncvm/tests/codeos_shim.h) already documents.
 * Tests therefore use fresh temporary directories rather than relying on
 * truncation of a previous run's output.
 */
#ifndef CODEOS_FS_SHIM_H
#define CODEOS_FS_SHIM_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_WRONLY
#define O_WRONLY 0x1
#endif
#ifndef O_CREAT
#define O_CREAT  0x40
#endif
#ifndef O_TRUNC
#define O_TRUNC  0x200
#endif

static inline int sys_open(const char *path, int flags) {
    return open(path, flags, 0644);
}

static inline int sys_read(int fd, void *buf, int count) {
    return (int)read(fd, buf, (size_t)count);
}

static inline int sys_pwrite(int fd, const void *buf, int count) {
    return (int)write(fd, buf, (size_t)count);
}

static inline int sys_write(const void *buf, int count) {
    return (int)write(STDOUT_FILENO, buf, (size_t)count);
}

static inline int sys_close(int fd) { return close(fd); }
static inline int sys_unlink(const char *path) { return unlink(path); }
static inline int sys_mkdir(const char *path) { return mkdir(path, 0755); }

static inline int sys_readdir(const char *path, char *buf, int cap)
    __attribute__((unused));
static inline int sys_readdir(const char *path, char *buf, int cap) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int used = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t n = strlen(e->d_name);
        if (used + (int)n + 1 >= cap) break;
        memcpy(buf + used, e->d_name, n);
        buf[used + (int)n] = '\0';
        used += (int)n + 1;
    }
    closedir(d);
    return used;
}

#endif /* CODEOS_FS_SHIM_H */
