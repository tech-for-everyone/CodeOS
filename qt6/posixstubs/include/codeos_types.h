/* CodeOS POSIX Types - Basic type definitions */

#ifndef _CODEOS_TYPES_H
#define _CODEOS_TYPES_H

#include <stdint.h>

/* Basic POSIX types */
typedef int64_t off_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t mode_t;
typedef int32_t nlink_t;
typedef int64_t ino_t;
typedef int32_t dev_t;

/* Time types */
struct timespec {
    int64_t tv_sec;
    long tv_nsec;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

#endif /* _CODEOS_TYPES_H */
