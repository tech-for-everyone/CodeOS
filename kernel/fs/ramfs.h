#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include "vfs.h"

#define RAMFS_MAX_FILES 64
#define RAMFS_FILE_SIZE 4096

typedef struct {
    char path[VFS_PATH_MAX];
    char data[RAMFS_FILE_SIZE];
    int  len;
    int  used;
} ramfs_file_t;

void ramfs_init(void);
int  ramfs_write(const char *path, const char *data, int len);
int  ramfs_read(const char *path, char *buf, int max);

#endif
