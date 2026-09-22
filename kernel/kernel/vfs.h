#ifndef VFS_H
#define VFS_H
#include "types.h"
#include "spinlock.h"

#define VFS_MAX_PATH 256
#define VFS_MAX_FDS 512
#define VFS_MAX_MOUNTS 32
#define VFS_MAX_INODES 1024
#define VFS_MAX_DENTS 256

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000
#define S_IRUSR 0000400
#define S_IWUSR 0000200
#define S_IXUSR 0000100
#define S_IRGRP 0000040
#define S_IWGRP 0000020
#define S_IXGRP 0000010
#define S_IROTH 0000004
#define S_IWOTH 0000002
#define S_IXOTH 0000001

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400
#define O_NONBLOCK 0x0800

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4
#define DT_LNK     10

typedef struct vfs_ops {
    int (*open)(const char *path, int flags);
    int (*close)(int fd);
    int (*read)(int fd, void *buf, int count);
    int (*write)(int fd, const void *buf, int count);
    int (*seek)(int fd, int offset, int whence);
    int (*fstat)(int fd, void *stat);
    int (*mkdir_vfs)(const char *path);
    int (*unlink)(const char *path);
    int (*rename)(const char *old, const char *new);
    int (*readdir)(int fd, void *ent);
    int (*ioctl)(int fd, int req, void *arg);
} vfs_ops_t;

typedef struct {
    char name[64];
    char path[VFS_MAX_PATH];
    uint64_t inode;
    uint32_t mode;
    uint64_t size;
    int in_use;
    vfs_ops_t *ops;
} vfs_mount_t;

typedef struct {
    int id;
    char path[VFS_MAX_PATH];
    uint64_t inode;
    uint32_t mode;
    uint64_t size;
    int pos;
    int flags;
    int mount_id;
    int in_use;
    int pid;
} vfs_fd_t;

typedef struct {
    char name[256];
    uint8_t type;
    uint64_t inode;
} vfs_dirent_t;

void vfs_init(void);
int vfs_open(const char *path, int flags, ...);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, int count);
int vfs_write(int fd, const void *buf, int count);
int vfs_seek(int fd, int offset, int whence);
int vfs_fstat(int fd, void *buf);
int vfs_stat(const char *path, void *buf);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldp, const char *newp);
int vfs_readdir(int fd, void *buf);
int vfs_ioctl(int fd, int req, void *arg);
int vfs_mount(const char *path, vfs_ops_t *ops, const char *name);
int vfs_umount(const char *path);
#endif
