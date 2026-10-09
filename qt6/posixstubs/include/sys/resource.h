/* CodeOS POSIX Stubs - Resource limits */

#ifndef _CODEOS_SYS_RESOURCE_H
#define _CODEOS_SYS_RESOURCE_H

#include <stdint.h>

/* Resource limits */
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_NOFILE  7
#define RLIMIT_AS      9
#define RLIMIT_NPROC   6
#define RLIMIT_MEMLOCK 8

/* Resource usage */
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

struct rlimit {
    unsigned long rlim_cur;
    unsigned long rlim_max;
};

/* getrlimit - stub */
static inline int getrlimit(int resource, struct rlimit *rlim) {
    (void)resource;
    if (!rlim) return -1;
    rlim->rlim_cur = 65536;
    rlim->rlim_max = 65536;
    return 0;
}

static inline int setrlimit(int resource, const struct rlimit *rlim) {
    (void)resource; (void)rlim;
    return 0;
}

static inline int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) return -1;
    for (int i = 0; i < (int)(sizeof(struct rusage) / sizeof(long)); i++)
        ((long *)usage)[i] = 0;
    return 0;
}

#endif /* _CODEOS_SYS_RESOURCE_H */
