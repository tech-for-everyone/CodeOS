#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <stdint.h>
typedef long int time_t;
struct timeval { long tv_sec; long tv_usec; };
#endif
