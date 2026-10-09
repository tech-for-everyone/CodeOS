/* Reference VFS layer. Production code: kernel/fs.c */

#include "vfs.h"
#include "kernel/kprintf.h"
#include "kernel/fs.h"

static vfs_node_t root;

void vfs_init(void) {
    root.name[0] = '/';
    root.name[1] = 0;
    root.is_dir = 1;
    root.size = 0;
    root.parent = &root;
    root.first_child = 0;
    root.next_sibling = 0;
    extern void fs_init(void);
    fs_init();
}

int vfs_lookup(const char *path, vfs_node_t **out) {
    if (!path || !out)
        return -1;
    if (path[0] == '/' && path[1] == 0) {
        *out = &root;
        return 0;
    }
    extern int fs_resolve(const char *, int *);
    extern int fs_node_count(void);
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0 || idx >= fs_node_count()) {
        kprintf("vfs: lookup '%s' not found\n", path);
        return -1;
    }
    *out = (vfs_node_t *)(uintptr_t)idx;
    return 0;
}

int vfs_create_file(const char *path) {
    if (!path) {
        kprintf("vfs: create_file NULL path\n");
        return -1;
    }
    extern int fs_mkfile(const char *);
    int ret = fs_mkfile(path);
    if (ret < 0) {
        kprintf("vfs: create_file '%s' FAILED\n", path);
    }
    return ret;
}

int vfs_mkdir(const char *path) {
    if (!path) {
        kprintf("vfs: mkdir NULL path\n");
        return -1;
    }
    extern int fs_mkdir(const char *);
    int ret = fs_mkdir(path);
    if (ret < 0) {
        kprintf("vfs: mkdir '%s' FAILED\n", path);
    }
    return ret;
}

int vfs_mount(const char *source, const char *target, const char *fstype) {
    if (!source || !target || !fstype) {
        kprintf("vfs: mount NULL argument\n");
        return -1;
    }
    kprintf("vfs: mount %s on %s type %s (not yet implemented)\n",
            source, target, fstype);
    (void)source;
    (void)target;
    (void)fstype;
    return -1;
}

int vfs_open(const char *path, const char *mode) {
    if (!path || !mode) {
        kprintf("vfs: open NULL argument\n");
        return -1;
    }
    kprintf("vfs: open '%s' mode '%s' (not yet implemented)\n", path, mode);
    (void)path;
    (void)mode;
    return -1;
}
