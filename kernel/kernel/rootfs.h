#ifndef ROOTFS_H
#define ROOTFS_H

#include "types.h"
#include "fs.h"

#define ROOTFS_MAX           16
#define ROOTFS_NAME_MAX      32
#define ROOTFS_PATH_MAX      128
#define ROOTFS_URL_MAX       256
#define ROOTFS_HASH_MAX      64
#define ROOTFS_CACHE_DIR     "/root/.codeos/rootfs"
#define ROOTFS_CONTAINER_DIR "/root/.codeos/containers"

typedef enum {
    ROOTFS_LINUX_DEBIAN = 0,
    ROOTFS_LINUX_ALPINE,
    ROOTFS_LINUX_FEDORA,
    ROOTFS_ANDROID_STOCK,
    ROOTFS_ANDROID_LINEAGE,
    ROOTFS_CUSTOM,
} rootfs_type_t;

typedef enum {
    ROOTFS_STATE_NONE = 0,
    ROOTFS_STATE_DOWNLOADING,
    ROOTFS_STATE_EXTRACTING,
    ROOTFS_STATE_READY,
    ROOTFS_STATE_ERROR,
    ROOTFS_STATE_OUTDATED,
} rootfs_state_t;

typedef struct {
    int          id;
    char         name[ROOTFS_NAME_MAX];
    rootfs_type_t type;
    rootfs_state_t state;
    char         path[ROOTFS_PATH_MAX];
    char         url[ROOTFS_URL_MAX];
    char         hash[ROOTFS_HASH_MAX];
    uint64_t     size_bytes;
    uint64_t     installed_size;
    int          version_major;
    int          version_minor;
    int          version_patch;
    uint64_t     last_updated;
    int          ref_count;      /* containers using this rootfs */
    char         arch[16];       /* x86_64, aarch64, etc. */
    char         init_binary[FS_PATH_MAX];
    int          is_readonly;
} rootfs_entry_t;

/* ─── Init ─── */
int rootfs_init(void);

/* ─── Rootfs management ─── */
int rootfs_register(const char *name, rootfs_type_t type, const char *url,
                    const char *hash, const char *arch);
int rootfs_unregister(int id);
int rootfs_download(int id);
int rootfs_extract(int id);
int rootfs_update(int id);
int rootfs_remove(int id);

/* ─── Container rootfs setup ─── */
int rootfs_create_container_rootfs(const char *rootfs_name,
                                   const char *container_name,
                                   char *out_path, int max);
int rootfs_setup_mounts(const char *rootfs_path, const char *container_path);
int rootfs_teardown_mounts(const char *container_path);

/* ─── Queries ─── */
rootfs_entry_t *rootfs_get(int id);
rootfs_entry_t *rootfs_find(const char *name);
int rootfs_list(char names[][ROOTFS_NAME_MAX], int max);
int rootfs_list_all(char names[][ROOTFS_NAME_MAX], int max);
int rootfs_get_state(int id);
const char *rootfs_type_str(rootfs_type_t type);
const char *rootfs_state_str(rootfs_state_t state);

/* ─── Pre-configured rootfs templates ─── */
int rootfs_add_debian_minimal(void);
int rootfs_add_alpine_minimal(void);
int rootfs_add_android_stock(void);

/* ─── Debian 8 (Jessie) rootfs extraction ─── */
int rootfs_extract_debian_minimal(void);

/* ─── Android stock container image  ─── */
int rootfs_seed_android_stock(void);

/* Bundled Android apps: installed into the android-stock image at
 * /system/app/<name>/<name> (copied from /bin/android-<name>) so the
 * image is complete out of the box. NULL-terminated; keep in sync with
 * ANDROID_PROGS in kernel/userspace/Makefile. */
#define ROOTFS_ANDROID_APP_COUNT 10
extern const char *const rootfs_android_apps[];

/* ─── Disk usage ─── */
uint64_t rootfs_get_disk_usage(int id);
uint64_t rootfs_get_total_disk_usage(void);

#endif
