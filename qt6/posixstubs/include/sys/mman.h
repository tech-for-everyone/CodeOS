/* CodeOS POSIX Stubs - Memory mapping (mmap/munmap) */

#ifndef _CODEOS_SYS_MMAN_H
#define _CODEOS_SYS_MMAN_H

#include <stdint.h>
#include <sys/types.h>

/* mmap flags */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_NORESERVE 0x4000

/* Protection flags */
#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4

/* mmap return value */
#define MAP_FAILED ((void *)-1)

/* msync flags */
#define MS_SYNC     0x0004
#define MS_ASYNC    0x0001
#define MS_INVALIDATE 0x0002

/* madvise flags */
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

/* mmap - uses CodeOS SYSCALL_MMAP */
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
    (void)fd;
    (void)offset;
    (void)flags;

    /* For now, use brk-based allocation if addr is NULL */
    if (addr == 0 && !(flags & MAP_FIXED)) {
        extern void *sys_brk(void *addr);
        void *p = sys_brk(0);  /* get current break */
        if (p && p != (void *)-1) {
            /* Align to page boundary */
            size_t aligned_len = (len + 4095) & ~4095;
            void *new_brk = sys_brk((char *)p + aligned_len);
            if (new_brk && new_brk != (void *)-1) {
                return p;
            }
        }
        return MAP_FAILED;
    }

    return MAP_FAILED;
}

static inline int munmap(void *addr, size_t len) {
    (void)addr;
    (void)len;
    /* CodeOS doesn't support munmap yet - stub it */
    return 0;
}

static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr;
    (void)len;
    (void)prot;
    /* Stub - CodeOS doesn't support mprotect yet */
    return 0;
}

static inline int msync(void *addr, size_t len, int flags) {
    (void)addr;
    (void)len;
    (void)flags;
    return 0;
}

static inline int madvise(void *addr, size_t len, int advice) {
    (void)addr;
    (void)len;
    (void)advice;
    return 0;
}

static inline void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...) {
    (void)old_address;
    (void)old_size;
    (void)new_size;
    (void)flags;
    return MAP_FAILED;
}

static inline int mlock(const void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

static inline int munlock(const void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

static inline int mlockall(int flags) {
    (void)flags;
    return 0;
}

static inline int munlockall(void) {
    return 0;
}

#endif /* _CODEOS_SYS_MMAN_H */
