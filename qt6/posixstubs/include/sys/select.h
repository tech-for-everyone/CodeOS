/* CodeOS POSIX Stubs - select/poll */

#ifndef _CODEOS_SYS_SELECT_H
#define _CODEOS_SYS_SELECT_H

#include <stdint.h>

/* fd_set */
#define FD_SETSIZE 1024

typedef struct {
    uint64_t fds[FD_SETSIZE / 64];
} fd_set;

#define FD_ZERO(set) do { for (int _i = 0; _i < FD_SETSIZE/64; _i++) (set)->fds[_i] = 0; } while(0)
#define FD_SET(fd, set) do { if (fd >= 0 && fd < FD_SETSIZE) (set)->fds[(fd)/64] |= (1ULL << ((fd)%64)); } while(0)
#define FD_CLR(fd, set) do { if (fd >= 0 && fd < FD_SETSIZE) (set)->fds[(fd)/64] &= ~(1ULL << ((fd)%64)); } while(0)
#define FD_ISSET(fd, set) ((fd >= 0 && fd < FD_SETSIZE) ? ((set)->fds[(fd)/64] & (1ULL << ((fd)%64))) : 0)

/* timeval */
struct timeval {
    long tv_sec;
    long tv_usec;
};

/* select - stub (returns immediately, no blocking I/O in CodeOS yet) */
static inline int select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
    /* No files ready, but don't block forever if timeout is set */
    if (timeout) {
        /* Just return 0 (no fds ready) after timeout */
        return 0;
    }
    return 0;
}

#endif /* _CODEOS_SYS_SELECT_H */
