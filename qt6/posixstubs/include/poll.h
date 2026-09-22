/* CodeOS POSIX Stubs - poll */

#ifndef _CODEOS_POLL_H
#define _CODEOS_POLL_H

#include <stdint.h>

/* poll events */
#define POLLIN      0x001
#define POLLPRI     0x002
#define POLLOUT     0x004
#define POLLERR     0x008
#define POLLHUP     0x010
#define POLLNVAL    0x020
#define POLLRDNORM  0x040
#define POLLRDBAND  0x080
#define POLLWRNORM  0x100
#define POLLWRBAND  0x200

/* pollfd */
struct pollfd {
    int fd;
    short events;
    short revents;
};

/* poll - stub (returns immediately) */
static inline int poll(struct pollfd *fds, int nfds, int timeout) {
    (void)fds; (void)nfds; (void)timeout;
    /* No fds ready */
    return 0;
}

/* ppoll - stub */
static inline int ppoll(struct pollfd *fds, int nfds,
                        const struct timespec *timeout, const void *sigmask) {
    (void)fds; (void)nfds; (void)timeout; (void)sigmask;
    return 0;
}

#endif /* _CODEOS_POLL_H */
