#include "unistd.h"
#include "errno.h"
#include "string.h"

int errno = 0;

static const struct {
    int num;
    const char *msg;
} errno_msgs[] = {
    { EPERM,    "Operation not permitted" },
    { ENOENT,   "No such file or directory" },
    { ESRCH,    "No such process" },
    { EINTR,    "Interrupted system call" },
    { EIO,      "I/O error" },
    { E2BIG,    "Argument list too long" },
    { ENOMEM,   "Out of memory" },
    { EACCES,   "Permission denied" },
    { EFAULT,   "Bad address" },
    { EEXIST,   "File exists" },
    { ENOTDIR,  "Not a directory" },
    { EISDIR,   "Is a directory" },
    { EINVAL,   "Invalid argument" },
    { ENFILE,   "Too many open files" },
    { EMFILE,   "Too many open files" },
    { EFBIG,    "File too large" },
    { ENOSPC,   "No space left on device" },
    { ESPIPE,   "Illegal seek" },
    { EROFS,    "Read-only file system" },
    { ENOSYS,   "Function not implemented" },
    { ENOTEMPTY,"Directory not empty" },
};

void perror(const char *s) {
    const char *msg = "Unknown error";
    for (unsigned int i = 0; i < sizeof(errno_msgs) / sizeof(errno_msgs[0]); i++) {
        if (errno_msgs[i].num == errno) {
            msg = errno_msgs[i].msg;
            break;
        }
    }
    if (s && s[0]) {
        sys_write(s, strlen(s));
        sys_write(": ", 2);
    }
    sys_write(msg, strlen(msg));
    sys_write("\n", 1);
}
