/* CodeOS POSIX Stubs - Wait */

#ifndef _CODEOS_SYS_WAIT_H
#define _CODEOS_SYS_WAIT_H

#include <stdint.h>

/* Wait status macros */
#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WIFSIGNALED(status) (((signed char)(((status) & 0x7f) + 1) >> 1) > 0)
#define WIFSTOPPED(status)  (((status) & 0xff) == 0x7f)
#define WIFCONTINUED(status) (status == 0xffff)
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WTERMSIG(status)    ((status) & 0x7f)
#define WSTOPSIG(status)    WEXITSTATUS(status)

/* Wait options */
#define WNOHANG   1
#define WUNTRACED 2
#define WCONTINUED 8
#define WNOWAIT   0x01000000

/* waitpid - uses CodeOS SYSCALL_WAIT */
static inline int waitpid(int pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    /* Stub - CodeOS has basic wait support */
    if (status) *status = 0;
    return 0;
}

static inline int wait(int *status) {
    return waitpid(-1, status, 0);
}

static inline int waitid(int idtype, int id, void *infop, int options) {
    (void)idtype; (void)id; (void)infop; (void)options;
    return 0;
}

#endif /* _CODEOS_SYS_WAIT_H */
