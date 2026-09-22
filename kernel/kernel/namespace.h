#ifndef NAMESPACE_H
#define NAMESPACE_H

#include "types.h"

#define NS_MAX         256
#define NS_NAME_MAX    32
#define CGROUP_MAX     64
#define CGROUP_NAME_MAX 64
#define NS_MAX_PROCS   256

/* ─── Namespace types ─── */
typedef enum {
    NS_TYPE_MOUNT = 0,
    NS_TYPE_PID,
    NS_TYPE_NETWORK,
    NS_TYPE_USER,
    NS_TYPE_IPC,
    NS_TYPE_UTS,
    NS_TYPE_CGROUP,
    NS_TYPE_COUNT
} ns_type_t;

/* ─── Mount point (for mount namespace) ─── */
#define MOUNT_PATH_MAX 128
#define MOUNT_OPTS_MAX 64

typedef struct mount_point {
    char source[MOUNT_PATH_MAX];
    char target[MOUNT_PATH_MAX];
    char fstype[32];
    char opts[MOUNT_OPTS_MAX];
    int  flags;
    struct mount_point *next;
} mount_point_t;

/* ─── Namespace ─── */
typedef struct {
    int      id;
    ns_type_t type;
    char     name[NS_NAME_MAX];
    int      ref_count;
    int      procs[NS_MAX_PROCS];
    int      proc_count;

    /* Mount namespace: list of mount points */
    mount_point_t *mounts;
    int            mount_count;

    /* PID namespace: pid offset (virtual pid = real pid - pid_offset) */
    int pid_offset;
    int pid_ns_level;

    /* Network namespace: isolated network stack */
    uint32_t net_ip;
    uint32_t net_mask;
    uint32_t net_gateway;
    int      net_ns_id;

    /* User namespace: UID/GID mapping */
    int uid_offset;
    int gid_offset;

    /* UTS namespace: hostname */
    char hostname[64];
} namespace_t;

/* ─── Cgroup resource limits ─── */
typedef struct {
    int      id;
    char     name[CGROUP_NAME_MAX];
    int      parent_id;

    /* CPU limits */
    int      cpu_shares;    /* 1-10000, default 1000 */
    int      cpu_quota_us;  /* microseconds per period, -1 = unlimited */
    int      cpu_period_us; /* period length, default 100000 (100ms) */
    int      cpu_set_cpus;  /* bitmask of allowed CPUs */

    /* Memory limits */
    uint64_t mem_limit;     /* bytes, 0 = unlimited */
    uint64_t mem_soft_limit;
    uint64_t mem_usage;
    int      oom_kill_disable;

    /* I/O limits */
    uint64_t blkio_read_bps;
    uint64_t blkio_write_bps;

    /* PID limits */
    int      pids_max;
    int      pids_current;

    /* Processes in this cgroup */
    int      procs[NS_MAX_PROCS];
    int      proc_count;
} cgroup_t;

/* ─── Init ─── */
int ns_init(void);

/* ─── Namespace operations ─── */
int      ns_create(ns_type_t type, const char *name);
int      ns_get(int ns_id);
int      ns_put(int ns_id);
int      ns_add_proc(int ns_id, int pid);
int      ns_remove_proc(int ns_id, int pid);
namespace_t *ns_get_by_id(int ns_id);
namespace_t *ns_get_by_type(ns_type_t type, int index);
int      ns_get_count(ns_type_t type);

/* ─── Mount namespace ─── */
int ns_mount(int ns_id, const char *source, const char *target,
             const char *fstype, int flags, const char *opts);
int ns_umount(int ns_id, const char *target);
mount_point_t *ns_get_mounts(int ns_id);
int ns_mount_count(int ns_id);

/* ─── Cgroup operations ─── */
int      cgroup_create(const char *name, int parent_id);
int      cgroup_destroy(int id);
int      cgroup_add_proc(int cg_id, int pid);
int      cgroup_remove_proc(int cg_id, int pid);
cgroup_t *cgroup_get(int id);
cgroup_t *cgroup_find(const char *name);

/* ─── Cgroup resource setters ─── */
int cgroup_set_cpu_shares(int cg_id, int shares);
int cgroup_set_cpu_quota(int cg_id, int quota_us, int period_us);
int cgroup_set_memory_limit(int cg_id, uint64_t limit);
int cgroup_set_pids_max(int cg_id, int max);

/* ─── Cgroup enforcement ─── */
int cgroup_check_memory(int cg_id, uint64_t add_bytes);
int cgroup_check_pids(int cg_id);

/* ─── Clone flags (for creating namespaces) ─── */
#define CLONE_NEWNS     (1 << 0)
#define CLONE_NEWPID    (1 << 1)
#define CLONE_NEWNET    (1 << 2)
#define CLONE_NEWUSER   (1 << 3)
#define CLONE_NEWIPC    (1 << 4)
#define CLONE_NEWUTS    (1 << 5)
#define CLONE_NEWCGROUP (1 << 6)
#define CLONE_ALL       0x7F

#endif
