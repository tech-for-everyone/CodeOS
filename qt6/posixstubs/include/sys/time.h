/* CodeOS POSIX Stubs - Time structures */

#ifndef _CODEOS_SYS_TIME_H
#define _CODEOS_SYS_TIME_H

#include <stdint.h>

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

/* gettimeofday - use CodeOS timer */
static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (!tv) return -1;

    extern uint64_t timer_get_milliseconds(void);
    uint64_t ms = timer_get_milliseconds();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

/* setitimer - stub */
struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

static inline int getitimer(int which, struct itimerval *curr_value) {
    (void)which; (void)curr_value;
    return 0;
}

static inline int setitimer(int which, const struct itimerval *new_value,
                            struct itimerval *old_value) {
    (void)which; (void)new_value; (void)old_value;
    return 0;
}

#endif /* _CODEOS_SYS_TIME_H */
