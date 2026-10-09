#include "vfs.h"
#include "process.h"
#include "kprintf.h"
#include "string.h"

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_fd_t fds[VFS_MAX_FDS];
static int mount_count = 0;
void vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    memset(fds, 0, sizeof(fds));
    mount_count = 0;
    kprintf("vfs: initialized (%d fds, %d mounts)\n", VFS_MAX_FDS, VFS_MAX_MOUNTS);
}

static int find_mount(const char *path) {
    int best = -1;
    int best_len = 0;
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].in_use) continue;
        int len = strlen(mounts[i].path);
        if (strncmp(path, mounts[i].path, len) == 0 && len > best_len) {
            best = i; best_len = len;
        }
    }
    return best;
}

int vfs_open(const char *path, int flags, ...) {
    if (!path) return -1;
    int mi = find_mount(path);
    if (mi < 0) return -1;
    int id = -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fds[i].in_use) { id = i; break; }
    }
    if (id < 0) return -1;
    memset(&fds[id], 0, sizeof(vfs_fd_t));
    fds[id].id = id;
    strncpy_safe(fds[id].path, path, VFS_MAX_PATH);
    fds[id].flags = flags;
    fds[id].mount_id = mi;
    fds[id].pos = 0;
    fds[id].in_use = 1;
    fds[id].pid = current_process ? current_process->pid : 0;
    if (mounts[mi].ops && mounts[mi].ops->open) {
        int r = mounts[mi].ops->open(path, flags);
        if (r < 0) { fds[id].in_use = 0; return r; }
    }
    return id;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    if (!fds[fd].in_use) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mi < mount_count && mounts[mi].ops && mounts[mi].ops->close)
        mounts[mi].ops->close(fd);
    fds[fd].in_use = 0;
    return 0;
}

int vfs_read(int fd, void *buf, int count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use || !buf) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->read)
        return mounts[mi].ops->read(fd, buf, count);
    return -1;
}

int vfs_write(int fd, const void *buf, int count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use || !buf) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->write)
        return mounts[mi].ops->write(fd, buf, count);
    return -1;
}

int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->seek)
        return mounts[mi].ops->seek(fd, offset, whence);
    return -1;
}

int vfs_fstat(int fd, void *buf) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->fstat)
        return mounts[mi].ops->fstat(fd, buf);
    return -1;
}

int vfs_stat(const char *path, void *buf) {
    if (!path) return -1;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -1;
    int r = vfs_fstat(fd, buf);
    vfs_close(fd);
    return r;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path) return -1;
    int mi = find_mount(path);
    if (mi < 0) return -1;
    if (mounts[mi].ops && mounts[mi].ops->mkdir_vfs)
        return mounts[mi].ops->mkdir_vfs(path);
    (void)mode;
    return -1;
}

int vfs_unlink(const char *path) {
    if (!path) return -1;
    int mi = find_mount(path);
    if (mi < 0) return -1;
    if (mounts[mi].ops && mounts[mi].ops->unlink)
        return mounts[mi].ops->unlink(path);
    return -1;
}

int vfs_rename(const char *oldp, const char *newp) {
    if (!oldp || !newp) return -1;
    int mi = find_mount(oldp);
    if (mi < 0) return -1;
    if (mounts[mi].ops && mounts[mi].ops->rename)
        return mounts[mi].ops->rename(oldp, newp);
    return -1;
}

int vfs_readdir(int fd, void *buf) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->readdir)
        return mounts[mi].ops->readdir(fd, buf);
    return -1;
}

int vfs_ioctl(int fd, int req, void *arg) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].in_use) return -1;
    int mi = fds[fd].mount_id;
    if (mi >= 0 && mounts[mi].ops && mounts[mi].ops->ioctl)
        return mounts[mi].ops->ioctl(fd, req, arg);
    return -1;
}

int vfs_mount(const char *path, vfs_ops_t *ops, const char *name) {
    if (!path || !ops) return -1;
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].in_use) continue;
        if (strcmp(mounts[i].path, path) == 0) return -1;
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].in_use) {
            memset(&mounts[i], 0, sizeof(vfs_mount_t));
            strncpy_safe(mounts[i].path, path, VFS_MAX_PATH);
            if (name) strncpy_safe(mounts[i].name, name, sizeof(mounts[i].name));
            mounts[i].ops = ops;
            mounts[i].in_use = 1;
            if (i >= mount_count) mount_count = i + 1;
            kprintf("vfs: mount %s at %s\n", name ? name : "?", path);
            return 0;
        }
    }
    return -1;
}

int vfs_umount(const char *path) {
    if (!path) return -1;
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].in_use && strcmp(mounts[i].path, path) == 0) {
            mounts[i].in_use = 0;
            kprintf("vfs: umount %s\n", path);
            return 0;
        }
    }
    return -1;
}
