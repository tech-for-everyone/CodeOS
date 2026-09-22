#ifndef ANDROID_CONTAINER_H
#define ANDROID_CONTAINER_H

#include "types.h"
#include "container.h"
#include "vm_manager.h"

#define ANDROID_MAX_CONTAINERS  8
#define ANDROID_NAME_MAX        32
#define ANDROID_IMAGE_PATH_MAX  128
#define ANDROID_PROP_MAX        256
#define ANDROID_INIT_RC_MAX     32

/* ─── Android container type ─── */
typedef enum {
    ANDROID_CONTAINER_SYSTEM = 0,  /* Full Android system container */
    ANDROID_CONTAINER_RUNTIME,     /* ART runtime container */
    ANDROID_CONTAINER_VM,          /* Android in a VM (ARCVM) */
} android_container_type_t;

/* ─── Android container state ─── */
typedef enum {
    ANDROID_STATE_NONE = 0,
    ANDROID_STATE_CREATING,
    ANDROID_STATE_BOOTING,
    ANDROID_STATE_RUNNING,
    ANDROID_STATE_PAUSED,
    ANDROID_STATE_STOPPING,
    ANDROID_STATE_STOPPED,
    ANDROID_STATE_CRASHED,
} android_container_state_t;

/* ─── Android container config ─── */
typedef struct {
    char name[ANDROID_NAME_MAX];
    android_container_type_t type;
    char system_image[ANDROID_IMAGE_PATH_MAX];
    char vendor_image[ANDROID_IMAGE_PATH_MAX];
    char data_image[ANDROID_IMAGE_PATH_MAX];
    char init_rc_path[ANDROID_IMAGE_PATH_MAX];
    uint32_t memory_mb;
    int  cpu_count;
    int  enable_gpu;
    int  enable_network;
    int  enable_audio;
    int  selinux_mode;       /* 0=permissive, 1=enforcing */
    char bootprop[ANDROID_PROP_MAX];
} android_container_config_t;

/* ─── Android container instance ─── */
typedef struct {
    int id;
    char name[ANDROID_NAME_MAX];
    android_container_type_t type;
    android_container_state_t state;
    int container_id;        /* underlying container */
    int vm_id;               /* underlying VM (for VM type) */
    uint32_t ip;
    uint64_t start_time;
    int exit_code;
    char rootfs[ANDROID_IMAGE_PATH_MAX];
    char data_dir[ANDROID_IMAGE_PATH_MAX];
    int display_width;
    int display_height;
    int display_bpp;
    uint64_t mem_used;
    uint64_t cpu_ticks;
} android_container_t;

/* ─── Init ─── */
int android_container_init(void);

/* ─── Container lifecycle ─── */
int android_container_create(const android_container_config_t *config);
int android_container_start(int id);
int android_container_stop(int id);
int android_container_destroy(int id);
int android_container_pause(int id);
int android_container_resume(int id);

/* ─── Container queries ─── */
android_container_t *android_container_get(int id);
android_container_t *android_container_find(const char *name);
int android_container_list(char names[][ANDROID_NAME_MAX], int max);
int android_container_get_state(int id);
const char *android_container_state_str(android_container_state_t state);

/* ─── Container exec ─── */
int android_container_exec(int id, const char *path, int argc, char **argv);

/* ─── Container info ─── */
int android_container_inspect(int id, char *buf, int max);
int android_container_logs(int id, char *buf, int max);

/* ─── Pre-configured templates ─── */
int android_container_create_stock(const char *name, uint64_t memory_mb);
int android_container_create_lineage(const char *name, uint64_t memory_mb);
int android_container_create_minimal(const char *name, uint64_t memory_mb);

/* ─── Container mount setup (ARC-style) ─── */
int android_container_setup_mounts(int id);
int android_container_setup_system_mounts(int id);
int android_container_setup_data_mounts(int id);
int android_container_setup_runtime_mounts(int id);

/* ─── Property management ─── */
int android_container_set_property(int id, const char *name, const char *value);
int android_container_get_property(int id, const char *name, char *value, int max);

#endif
