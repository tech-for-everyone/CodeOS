/* In-memory RAM filesystem reference. Production ramfs: kernel/fs.c */

#include "ramfs.h"

static ramfs_file_t files[RAMFS_MAX_FILES];

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        files[i].used = 0;
}

static ramfs_file_t *ramfs_find(const char *path) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used)
            continue;
        int j = 0;
        while (path[j] && files[i].path[j] && path[j] == files[i].path[j])
            j++;
        if (path[j] == 0 && files[i].path[j] == 0)
            return &files[i];
    }
    return 0;
}

static ramfs_file_t *ramfs_alloc(const char *path) {
    ramfs_file_t *f = ramfs_find(path);
    if (f)
        return f;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].used)
            continue;
        int j = 0;
        while (path[j] && j < VFS_PATH_MAX - 1) {
            files[i].path[j] = path[j];
            j++;
        }
        files[i].path[j] = 0;
        files[i].len = 0;
        files[i].used = 1;
        return &files[i];
    }
    return 0;
}

int ramfs_write(const char *path, const char *data, int len) {
    if (!path || !data || len < 0)
        return -1;
    if (len > RAMFS_FILE_SIZE)
        len = RAMFS_FILE_SIZE;
    ramfs_file_t *f = ramfs_alloc(path);
    if (!f)
        return -1;
    for (int i = 0; i < len; i++)
        f->data[i] = data[i];
    f->len = len;
    return len;
}

int ramfs_read(const char *path, char *buf, int max) {
    if (!path || !buf || max <= 0)
        return -1;
    ramfs_file_t *f = ramfs_find(path);
    if (!f)
        return -1;
    int n = f->len;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        buf[i] = f->data[i];
    return n;
}
