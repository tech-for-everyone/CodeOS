#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WIFEXITED(status) (((status) & 0xff) == 0)

#endif
