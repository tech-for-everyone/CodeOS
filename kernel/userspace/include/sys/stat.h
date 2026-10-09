#ifndef SYS_STAT_H
#define SYS_STAT_H

#include "unistd.h"

#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFREG  0x8000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

static inline int stat(const char *path, stat_t *buf) {
    return sys_stat(path, buf);
}

#endif
