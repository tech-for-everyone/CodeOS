#ifndef ERRNO_H
#define ERRNO_H

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define E2BIG   7
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define ENOSYS  38
#define ENOTEMPTY 39

extern int errno;

void perror(const char *s);

#endif
