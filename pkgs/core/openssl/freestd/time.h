#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
typedef long int time_t;
struct timespec { long tv_sec; long tv_nsec; };
time_t time(time_t *tloc);
int nanosleep(const struct timespec *req, struct timespec *rem);
char *ctime(const time_t *t);
#endif
