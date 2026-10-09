#ifndef TIME_H
#define TIME_H

#include "unistd.h"

typedef int64_t time_t;
typedef timeval_t timeval;

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

static inline time_t time(time_t *tloc) {
    int64_t sec = sys_time();
    if (tloc) *tloc = (time_t)sec;
    return (time_t)sec;
}

static inline int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    return sys_gettimeofday((timeval_t *)tv);
}

#endif
