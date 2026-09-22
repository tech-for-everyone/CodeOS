#include "namespace.h"
#include "kprintf.h"
#include "string.h"

static namespace_t namespaces[NS_MAX];
static int ns_next_id = 1;

static cgroup_t cgroups[CGROUP_MAX];
static int cg_next_id = 1;

/* ══════════════════════════════════════════════════════════════════════════════
 * Init
 * ══════════════════════════════════════════════════════════════════════════════ */

int ns_init(void) {
    memset(namespaces, 0, sizeof(namespaces));
    memset(cgroups, 0, sizeof(cgroups));

    /* Create root namespace for each type (held by kernel/init process) */
    for (int t = 0; t < NS_TYPE_COUNT; t++) {
        namespace_t *ns = &namespaces[t];
        ns->id = ns_next_id++;
        ns->type = (ns_type_t)t;
        ns->ref_count = 1;
        ns->proc_count = 0;
        /* Init mount namespace with root mount */
        if (t == NS_TYPE_MOUNT) {
            /* Root mount is implicit (initramfs) */
        }
        /* Init UTS namespace */
        if (t == NS_TYPE_UTS) {
            strlcpy(ns->hostname, "codeos", sizeof(ns->hostname));
        }
        /* Init network namespace */
        if (t == NS_TYPE_NETWORK) {
            ns->net_ip = 0x0A000001; /* 10.0.0.1 */
            ns->net_mask = 0xFFFFFF00;
            ns->net_gateway = 0x0A000001;
        }
        /* Init PID namespace */
        if (t == NS_TYPE_PID) {
            ns->pid_offset = 0;
            ns->pid_ns_level = 0;
        }
        /* Init user namespace */
        if (t == NS_TYPE_USER) {
            ns->uid_offset = 0;
            ns->gid_offset = 0;
        }
    }

    /* Create root cgroup */
    cgroup_t *root = &cgroups[0];
    root->id = cg_next_id++;
    strlcpy(root->name, "/", CGROUP_NAME_MAX);
    root->parent_id = 0;
    root->cpu_shares = 1000;
    root->cpu_quota_us = -1;
    root->cpu_period_us = 100000;
    root->mem_limit = 0;
    root->pids_max = -1;
    root->proc_count = 0;

    kprintf("ns: initialized %d namespaces, root cgroup\n", NS_TYPE_COUNT);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Namespace operations
 * ══════════════════════════════════════════════════════════════════════════════ */

static int alloc_ns_slot(void) {
    for (int i = NS_TYPE_COUNT; i < NS_MAX; i++)
        if (namespaces[i].id == 0) return i;
    return -1;
}

int ns_create(ns_type_t type, const char *name) {
    if (type >= NS_TYPE_COUNT) return -1;
    int slot = alloc_ns_slot();
    if (slot < 0) return -1;

    namespace_t *ns = &namespaces[slot];
    memset(ns, 0, sizeof(namespace_t));
    ns->id = ns_next_id++;
    ns->type = type;
    ns->ref_count = 1;
    if (name) strlcpy(ns->name, name, NS_NAME_MAX);

    /* Copy defaults from root namespace of same type */
    namespace_t *root = &namespaces[type];
    switch (type) {
        case NS_TYPE_MOUNT:
            /* New mount namespace starts empty (will be populated) */
            break;
        case NS_TYPE_PID:
            ns->pid_offset = 0;
            ns->pid_ns_level = root->pid_ns_level + 1;
            break;
        case NS_TYPE_NETWORK:
            ns->net_ip = 0x0A000001 + (slot * 0x100);
            ns->net_mask = 0xFFFFFF00;
            ns->net_gateway = 0x0A000001;
            break;
        case NS_TYPE_USER:
            ns->uid_offset = 0;
            ns->gid_offset = 0;
            break;
        case NS_TYPE_UTS:
            strlcpy(ns->hostname, root->hostname, sizeof(ns->hostname));
            break;
        default:
            break;
    }

            kprintf("ns: created %s namespace id=%d name='%s'\n",
            type == NS_TYPE_MOUNT ? "mount" :
            type == NS_TYPE_PID ? "pid" :
            type == NS_TYPE_NETWORK ? "net" :
            type == NS_TYPE_USER ? "user" :
            type == NS_TYPE_UTS ? "uts" :
            type == NS_TYPE_IPC ? "ipc" :
            type == NS_TYPE_CGROUP ? "cgroup" : "?",
            ns->id, ns->name);
    return ns->id;
}

int ns_get(int ns_id) {
    for (int i = 0; i < NS_MAX; i++) {
        if (namespaces[i].id == ns_id) {
            namespaces[i].ref_count++;
            return ns_id;
        }
    }
    return -1;
}

int ns_put(int ns_id) {
    for (int i = NS_TYPE_COUNT; i < NS_MAX; i++) {
        if (namespaces[i].id == ns_id) {
            if (--namespaces[i].ref_count <= 0) {
                /* Free mount points */
                mount_point_t *mp = namespaces[i].mounts;
                while (mp) {
                    mount_point_t *next = mp->next;
                    memset(mp, 0, sizeof(mount_point_t));
                    mp = next;
                }
                memset(&namespaces[i], 0, sizeof(namespace_t));
            }
            return 0;
        }
    }
    return -1;
}

int ns_add_proc(int ns_id, int pid) {
    for (int i = 0; i < NS_MAX; i++) {
        if (namespaces[i].id == ns_id) {
            if (namespaces[i].proc_count >= NS_MAX_PROCS) return -1;
            namespaces[i].procs[namespaces[i].proc_count++] = pid;
            return 0;
        }
    }
    return -1;
}

int ns_remove_proc(int ns_id, int pid) {
    for (int i = 0; i < NS_MAX; i++) {
        if (namespaces[i].id == ns_id) {
            for (int j = 0; j < namespaces[i].proc_count; j++) {
                if (namespaces[i].procs[j] == pid) {
                    namespaces[i].procs[j] =
                        namespaces[i].procs[--namespaces[i].proc_count];
                    return 0;
                }
            }
            return -1;
        }
    }
    return -1;
}

namespace_t *ns_get_by_id(int ns_id) {
    for (int i = 0; i < NS_MAX; i++)
        if (namespaces[i].id == ns_id) return &namespaces[i];
    return 0;
}

namespace_t *ns_get_by_type(ns_type_t type, int index) {
    int count = 0;
    for (int i = 0; i < NS_MAX; i++) {
        if (namespaces[i].id > 0 && namespaces[i].type == type) {
            if (count == index) return &namespaces[i];
            count++;
        }
    }
    return 0;
}

int ns_get_count(ns_type_t type) {
    int count = 0;
    for (int i = 0; i < NS_MAX; i++)
        if (namespaces[i].id > 0 && namespaces[i].type == type) count++;
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Mount namespace
 * ══════════════════════════════════════════════════════════════════════════════ */

int ns_mount(int ns_id, const char *source, const char *target,
             const char *fstype, int flags, const char *opts) {
    namespace_t *ns = ns_get_by_id(ns_id);
    if (!ns || ns->type != NS_TYPE_MOUNT) return -1;

    /* Allocate mount point from kernel heap (use a static pool for now) */
    static mount_point_t mount_pool[64];
    static int mount_pool_idx = 0;
    if (mount_pool_idx >= 64) return -1;

    mount_point_t *mp = &mount_pool[mount_pool_idx++];
    memset(mp, 0, sizeof(mount_point_t));
    strlcpy(mp->source, source, MOUNT_PATH_MAX);
    strlcpy(mp->target, target, MOUNT_PATH_MAX);
    if (fstype) strlcpy(mp->fstype, fstype, 32);
    if (opts) strlcpy(mp->opts, opts, MOUNT_OPTS_MAX);
    mp->flags = flags;

    /* Add to namespace mount list */
    mp->next = ns->mounts;
    ns->mounts = mp;
    ns->mount_count++;

    kprintf("ns: mounted %s -> %s (%s) in ns %d\n", source, target,
            fstype ? fstype : "none", ns_id);
    return 0;
}

int ns_umount(int ns_id, const char *target) {
    namespace_t *ns = ns_get_by_id(ns_id);
    if (!ns || ns->type != NS_TYPE_MOUNT) return -1;

    mount_point_t *prev = 0;
    mount_point_t *mp = ns->mounts;
    while (mp) {
        if (strcmp(mp->target, target) == 0) {
            if (prev) prev->next = mp->next;
            else ns->mounts = mp->next;
            ns->mount_count--;
            mp->next = 0;
            kprintf("ns: unmounted %s from ns %d\n", target, ns_id);
            return 0;
        }
        prev = mp;
        mp = mp->next;
    }
    return -1;
}

mount_point_t *ns_get_mounts(int ns_id) {
    namespace_t *ns = ns_get_by_id(ns_id);
    if (!ns || ns->type != NS_TYPE_MOUNT) return 0;
    return ns->mounts;
}

int ns_mount_count(int ns_id) {
    namespace_t *ns = ns_get_by_id(ns_id);
    if (!ns || ns->type != NS_TYPE_MOUNT) return 0;
    return ns->mount_count;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Cgroup operations
 * ══════════════════════════════════════════════════════════════════════════════ */

static int alloc_cg_slot(void) {
    for (int i = 1; i < CGROUP_MAX; i++)
        if (cgroups[i].id == 0) return i;
    return -1;
}

int cgroup_create(const char *name, int parent_id) {
    int slot = alloc_cg_slot();
    if (slot < 0) return -1;

    cgroup_t *cg = &cgroups[slot];
    memset(cg, 0, sizeof(cgroup_t));
    cg->id = cg_next_id++;
    strlcpy(cg->name, name, CGROUP_NAME_MAX);
    cg->parent_id = parent_id;

    /* Inherit limits from parent */
    if (parent_id > 0) {
        cgroup_t *parent = cgroup_get(parent_id);
        if (parent) {
            cg->cpu_shares = parent->cpu_shares;
            cg->cpu_quota_us = parent->cpu_quota_us;
            cg->cpu_period_us = parent->cpu_period_us;
            cg->mem_limit = parent->mem_limit;
            cg->pids_max = parent->pids_max;
        }
    } else {
        cg->cpu_shares = 1000;
        cg->cpu_quota_us = -1;
        cg->cpu_period_us = 100000;
        cg->pids_max = -1;
    }

    kprintf("ns: created cgroup '%s' id=%d parent=%d\n", name, cg->id, parent_id);
    return cg->id;
}

int cgroup_destroy(int id) {
    if (id <= 0 || id >= CGROUP_MAX) return -1;
    cgroup_t *cg = &cgroups[id];
    if (cg->id == 0) return -1;
    int saved = cg->id;
    memset(cg, 0, sizeof(cgroup_t));
    kprintf("ns: destroyed cgroup id=%d\n", saved);
    return 0;
}

int cgroup_add_proc(int cg_id, int pid) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    if (cg->proc_count >= NS_MAX_PROCS) return -1;
    if (cg->pids_max > 0 && cg->pids_current >= cg->pids_max) return -1;
    cg->procs[cg->proc_count++] = pid;
    cg->pids_current = cg->proc_count;
    return 0;
}

int cgroup_remove_proc(int cg_id, int pid) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    for (int i = 0; i < cg->proc_count; i++) {
        if (cg->procs[i] == pid) {
            cg->procs[i] = cg->procs[--cg->proc_count];
            cg->pids_current = cg->proc_count;
            return 0;
        }
    }
    return -1;
}

cgroup_t *cgroup_get(int id) {
    if (id <= 0 || id >= CGROUP_MAX) return 0;
    return cgroups[id].id > 0 ? &cgroups[id] : 0;
}

cgroup_t *cgroup_find(const char *name) {
    for (int i = 0; i < CGROUP_MAX; i++)
        if (cgroups[i].id > 0 && strcmp(cgroups[i].name, name) == 0)
            return &cgroups[i];
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Cgroup resource setters
 * ══════════════════════════════════════════════════════════════════════════════ */

int cgroup_set_cpu_shares(int cg_id, int shares) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    if (shares < 1) shares = 1;
    if (shares > 10000) shares = 10000;
    cg->cpu_shares = shares;
    return 0;
}

int cgroup_set_cpu_quota(int cg_id, int quota_us, int period_us) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    cg->cpu_quota_us = quota_us;
    if (period_us > 0) cg->cpu_period_us = period_us;
    return 0;
}

int cgroup_set_memory_limit(int cg_id, uint64_t limit) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    cg->mem_limit = limit;
    return 0;
}

int cgroup_set_pids_max(int cg_id, int max) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return -1;
    cg->pids_max = max;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Cgroup enforcement (called from alloc/free paths)
 * ══════════════════════════════════════════════════════════════════════════════ */

int cgroup_check_memory(int cg_id, uint64_t add_bytes) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return 0; /* no cgroup = no limit */
    if (cg->mem_limit == 0) return 0; /* unlimited */
    if (cg->mem_usage + add_bytes > cg->mem_limit) {
        kprintf("ns: cgroup '%s' memory limit exceeded (%lu + %lu > %lu)\n",
                cg->name, cg->mem_usage, add_bytes, cg->mem_limit);
        return -1;
    }
    cg->mem_usage += add_bytes;
    return 0;
}

int cgroup_check_pids(int cg_id) {
    cgroup_t *cg = cgroup_get(cg_id);
    if (!cg) return 0;
    if (cg->pids_max < 0) return 0;
    if (cg->pids_current >= cg->pids_max) {
        kprintf("ns: cgroup '%s' pids limit reached (%d >= %d)\n",
                cg->name, cg->pids_current, cg->pids_max);
        return -1;
    }
    return 0;
}
