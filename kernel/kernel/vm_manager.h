#ifndef VM_MANAGER_H
#define VM_MANAGER_H

#include "types.h"
#include "container.h"

#define VM_MAX              16
#define VM_NAME_MAX         32
#define VM_IMAGE_PATH_MAX   128
#define VM_SOCK_PATH_MAX    128
#define VM_MAX_VSOCKS       8

/* ─── VM states ─── */
typedef enum {
    VM_STATE_CREATED = 0,
    VM_STATE_BOOTING,
    VM_STATE_RUNNING,
    VM_STATE_PAUSED,
    VM_STATE_STOPPING,
    VM_STATE_STOPPED,
    VM_STATE_CRASHED,
} vm_state_t;

/* ─── VM type ─── */
typedef enum {
    VM_TYPE_LINUX_CONTAINER = 0,  /* Process-based isolation with namespaces */
    VM_TYPE_ANDROID_CONTAINER,    /* Android runtime in container */
    VM_TYPE_PLUGIN,               /* Third-party plugin VM */
    VM_TYPE_CROSVM,               /* crosvm-based virtual machine (Rust VMM) */
} vm_type_t;

/* ─── VM configuration ─── */
typedef struct {
    char     name[VM_NAME_MAX];
    vm_type_t type;
    char     image_path[VM_IMAGE_PATH_MAX];
    uint32_t memory_mb;       /* Memory limit in MB */
    int      cpu_count;       /* Number of virtual CPUs */
    char     rootfs_path[VM_IMAGE_PATH_MAX];
    char     kernel_path[VM_IMAGE_PATH_MAX];  /* For actual VMs */
    char     init_path[VM_IMAGE_PATH_MAX];    /* Init binary inside rootfs */
    int      enable_gpu;      /* GPU passthrough */
    int      enable_network;  /* Network access */
    int      enable_9p;       /* 9p file sharing */
    char     shared_path[VM_IMAGE_PATH_MAX];  /* Host path to share via 9p */
    char     shared_mount[VM_IMAGE_PATH_MAX]; /* Guest mount point */
} vm_config_t;

/* ─── VM instance ─── */
typedef struct {
    int          id;
    char         name[VM_NAME_MAX];
    vm_state_t   state;
    vm_type_t    type;
    int          container_id;  /* Associated container */
    uint64_t     start_time;
    uint64_t     stop_time;
    int          exit_code;

    /* Resource usage */
    uint64_t     mem_used;
    uint64_t     cpu_ticks;

    /* Network */
    uint32_t     ip;
    uint16_t     ssh_port;      /* Host port mapped to guest SSH */

    /* Display */
    int      display_width;
    int      display_height;
    int      display_bpp;

    /* Configuration snapshot */
    vm_config_t  config;
} vm_t;

/* ─── VM Manager init ─── */
int vm_manager_init(void);

/* ─── Launcher feedback sync (consume .pid/.exit from /tmp/crosvm-cmds) ─── */
void vm_sync_states(void);

/* ─── Spawn /bin/crosvm-launcher daemon on demand ─── */
int vm_daemon_start(void);
const char *vm_type_str(vm_type_t type);

/* ─── VM lifecycle ─── */
int vm_create(const vm_config_t *config);
int vm_start(int vm_id);
int vm_stop(int vm_id);
int vm_destroy(int vm_id);
int vm_pause(int vm_id);
int vm_resume(int vm_id);

/* ─── VM queries ─── */
vm_t *vm_get(int vm_id);
vm_t *vm_find(const char *name);
int   vm_list(char names[][VM_NAME_MAX], int max);
int   vm_list_all(char names[][VM_NAME_MAX], int max);
int   vm_get_state(int vm_id);
const char *vm_state_str(vm_state_t state);

/* ─── VM exec (run command inside VM) ─── */
int vm_exec(int vm_id, const char *path, int argc, char **argv, char **envp);

/* ─── VM info / inspect ─── */
int vm_inspect(int vm_id, char *buf, int max);
int vm_logs(int vm_id, char *buf, int max);

/* ─── VM resource limits ─── */
int vm_set_memory_limit(int vm_id, uint64_t mb);
int vm_set_cpu_count(int vm_id, int count);

/* ─── VM resource monitoring (polls /proc/{pid}/status) ─── */
int vm_get_memory_usage(int vm_id, uint64_t *rss_bytes);
int vm_get_cpu_usage(int vm_id, uint64_t *cpu_ticks);

/* ─── Pre-configured VM templates ─── */
int vm_create_linux(const char *name, const char *rootfs, uint64_t memory_mb);
int vm_create_android(const char *name, const char *rootfs, uint64_t memory_mb);
int vm_create_crosvm(const char *name, const char *kernel, const char *initrd,
                     const char *rootfs, uint64_t memory_mb, int cpu_count);

/* ─── Concierge-style API (inspired by ChromeOS vm_tools) ─── */
int vm_concierge_list(void);
int vm_concierge_create(const char *name, const char *image, uint64_t memory_mb);
int vm_concierge_create_crosvm(const char *name, const char *kernel, const char *initrd,
                               const char *rootfs, uint64_t memory_mb, int cpu_count);
int vm_concierge_start(const char *name);
int vm_concierge_stop(const char *name);
int vm_concierge_destroy(const char *name);
int vm_concierge_snapshot(const char *name, const char *snapshot_name);

#endif
