/* CodeOS POSIX Stubs - File status */

#ifndef _CODEOS_SYS_STAT_H
#define _CODEOS_SYS_STAT_H

#include <stdint.h>

/* File type bits */
#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

/* Permission bits */
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000
#define S_IRWXU  00700
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRWXG  00070
#define S_IRGRP  00040
#define S_IWGRP  00020
#define S_IXGRP  00010
#define S_IRWXO  00007
#define S_IROTH  00004
#define S_IWOTH  00002
#define S_IXOTH  00001

/* File type test macros */
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* stat structure (simplified for CodeOS) */
struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

/* fstat - stub that returns basic info */
static inline int fstat(int fd, struct stat *buf) {
    (void)fd;
    if (!buf) return -1;
    buf->st_dev = 0;
    buf->st_ino = fd;
    buf->st_mode = S_IFREG | 0644;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_rdev = 0;
    buf->st_size = 0;
    buf->st_blksize = 4096;
    buf->st_blocks = 0;
    buf->st_atime = 0;
    buf->st_mtime = 0;
    buf->st_ctime = 0;
    return 0;
}

static inline int stat(const char *path, struct stat *buf) {
    (void)path;
    return fstat(-1, buf);
}

static inline int lstat(const char *path, struct stat *buf) {
    return stat(path, buf);
}

static inline int mkdir(const char *path, int mode) {
    (void)path; (void)mode;
    return 0;  /* Stub - always succeed */
}

static inline int chmod(const char *path, int mode) {
    (void)path; (void)mode;
    return 0;
}

static inline int fchmod(int fd, int mode) {
    (void)fd; (void)mode;
    return 0;
}

static inline int chown(const char *path, int owner, int group) {
    (void)path; (void)owner; (void)group;
    return 0;
}

static inline int fchown(int fd, int owner, int group) {
    (void)fd; (void)owner; (void)group;
    return 0;
}

static inline int umask(int mask) {
    (void)mask;
    return 0022;
}

#endif /* _CODEOS_SYS_STAT_H */
