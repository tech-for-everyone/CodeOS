/* Host shim so the real apk-parser.c can be built and run against real files
 * on the build machine, instead of only inside a booted guest.
 *
 * This follows the pattern already used for ncvm
 * (pkgs/core/ncvm/tests/codeos_shim.h): compile the *real* source with
 * `main` intact and map the handful of CodeOS syscalls it uses onto POSIX.
 * apk-parser only uses sys_open/sys_read/sys_lseek, so that is all there is.
 *
 * Define APK_SHIM_FILE_CAP to emulate the kernel's FS_CONTENT_MAX clamp:
 * sys_lseek() on the real kernel refuses to move past 65536, and sys_open()
 * has already truncated the file to that many bytes by the time userspace
 * sees it. Without that emulation a >64 KiB archive would parse successfully
 * here and the truncation diagnostic could never be exercised.
 */
#ifndef APK_SHIM_H
#define APK_SHIM_H

#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef APK_SHIM_FILE_CAP
#include <stdlib.h>

/* Per-fd position, so the clamp can be applied the way the kernel applies
 * it: a running total, not a per-read limit. */
static long shim_pos[1024];
static long shim_len[1024];   /* what sys_open would have made visible */

static long shim_clamp(long v) {
    return v < 0 ? 0 : (v > (long)APK_SHIM_FILE_CAP ? (long)APK_SHIM_FILE_CAP : v);
}
#endif

static inline int sys_open(const char *path, int flags) {
    int f = (flags == 0) ? O_RDONLY : flags;
    int fd = open(path, f);
#ifdef APK_SHIM_FILE_CAP
    if (fd >= 0 && fd < 1024) {
        /* The kernel reads the whole file into a FS_CONTENT_MAX buffer at
         * open time, so the length userspace can ever see is min(real, CAP).
         * Using CAP unconditionally here -- which an earlier version of this
         * shim did -- makes a 24 KB file report as 65536 and sends the parser
         * down the truncation path for input that was never truncated. */
        off_t real = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        shim_len[fd] = shim_clamp((long)real);
        shim_pos[fd] = 0;
    }
#endif
    return fd;
}

static inline int sys_read(int fd, void *buf, int count) {
#ifdef APK_SHIM_FILE_CAP
    if (fd < 1024) {
        long room = (long)APK_SHIM_FILE_CAP - shim_pos[fd];
        if (room <= 0) return 0;
        if (count > room) count = (int)room;
        int n = (int)read(fd, buf, (size_t)count);
        if (n > 0 && fd < 1024) shim_pos[fd] += n;
        return n;
    }
#endif
    return (int)read(fd, buf, (size_t)count);
}

static inline int sys_lseek(int fd, int64_t off, int whence) {
    long np;
#ifdef APK_SHIM_FILE_CAP
    if (fd < 1024) {
        switch (whence) {
        case 0: np = (long)off; break;
        case 1: np = shim_pos[fd] + (long)off; break;
        default: np = shim_len[fd] + (long)off; break;
        }
        np = shim_clamp(np);
        if (lseek(fd, np, SEEK_SET) == (off_t)-1) return -1;
        shim_pos[fd] = np;
        return (int)np;
    }
#endif
    np = (long)lseek(fd, (off_t)off, whence);
    return (int)np;
}

#endif /* APK_SHIM_H */