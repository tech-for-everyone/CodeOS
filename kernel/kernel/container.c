#include "container.h"
#include "namespace.h"
#include "kprintf.h"
#include "string.h"
#include "rng.h"
#include "process.h"
#include "sched.h"
#include "elf.h"
#include "fs.h"
#include "pmm.h"
#include "vmm.h"
#include "umode.h"
#include "rootfs.h"
#include "shell.h"
#include "user_wm.h"
#include "../drivers/timer.h"
#include "../arch/x86_64/fb.h"

static container_t containers[CONTAINER_MAX];
static int container_next_id = 1;
static int container_next_ip  = 1;

int container_init(void) {
    memset(containers, 0, sizeof(containers));
    ns_init();
    kprintf("appvm: container engine ready (%d slots, namespaces + cgroups)\n", CONTAINER_MAX);
    return 0;
}

container_t *container_get(int id) {
    for (int i = 0; i < CONTAINER_MAX; i++)
        if (containers[i].id == id)
            return &containers[i];
    return 0;
}

container_t *container_find(const char *name) {
    for (int i = 0; i < CONTAINER_MAX; i++)
        if (containers[i].id > 0 && strcmp(containers[i].name, name) == 0)
            return &containers[i];
    return 0;
}

static int mkdirs(const char *path) {
    char tmp[FS_PATH_MAX];
    strlcpy(tmp, path, sizeof(tmp));
    char *p = tmp;
    if (*p == '/') p++;
    while (*p) {
        char *slash = p;
        while (*slash && *slash != '/') slash++;
        char save = *slash;
        *slash = 0;
        if (tmp[0]) {
            int is_dir;
            if (fs_resolve(tmp, &is_dir) < 0) {
                if (fs_mkdir(tmp) < 0) return -1;
            }
        }
        *slash = save;
        if (!save) break;
        p = slash + 1;
    }
    return 0;
}

static void container_log_append(container_t *c, const char *msg) {
    if (!c) return;
    while (*msg) {
        c->log[c->log_head] = *msg;
        c->log_head = (c->log_head + 1) % CONTAINER_LOG_SIZE;
        if (c->log_count < CONTAINER_LOG_SIZE) c->log_count++;
        msg++;
    }
}

static int alloc_slot(void) {
    for (int i = 0; i < CONTAINER_MAX; i++)
        if (containers[i].id == 0) return i;
    return -1;
}

/* ─── Create a container image from a root path ─── */
int container_create_image(const char *name, const char *root_path) {
    char img_root[FS_PATH_MAX];
    snprintf(img_root, sizeof(img_root), "/containers/images/%s", name);
    if (mkdirs(img_root) < 0) {
        kprintf("appvm: failed to create image dir '%s'\n", img_root);
        return -1;
    }
    char cfg[FS_PATH_MAX];
    snprintf(cfg, sizeof(cfg), "%s/config.json", img_root);
    fs_mkfile(cfg);
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "{\"image\":\"%s\",\"root\":\"%s\"}", name, root_path);
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    fs_write(cfg, buf, n);
    kprintf("appvm: image '%s' created from '%s'\n", name, root_path);
    return 0;
}

/* ─── Create a container with full namespace isolation ─── */
/* ─── Create container with resource limits ─── */
int container_create_with_limits(const char *name, const char *image, uint64_t memory_mb, int cpu_shares) {
    int id = container_create(name, image);
    if (id < 0) return -1;
    container_t *c = container_get(id);
    if (!c || c->cgroup_id < 0) return id;
    if (memory_mb > 0) {
        cgroup_set_memory_limit(c->cgroup_id, memory_mb * 1024 * 1024);
    }
    if (cpu_shares > 0) {
        cgroup_set_cpu_shares(c->cgroup_id, cpu_shares);
    }
    return id;
}

int container_create(const char *name, const char *image) {
    container_t *exist = container_find(name);
    if (exist) {
        kprintf("appvm: container '%s' already exists\n", name);
        return -1;
    }
    int slot = alloc_slot();
    if (slot < 0) {
        kprintf("appvm: no slots available\n");
        return -1;
    }

    char img_root[FS_PATH_MAX];
    snprintf(img_root, sizeof(img_root), "/containers/images/%s", image);
    int is_dir;
    if (fs_resolve(img_root, &is_dir) < 0) {
        /* Auto-bootstrap known images if missing */
        if (strcmp(image, "debian-minimal") == 0) {
            kprintf("appvm: image '%s' not found, auto-extracting...\n", image);
            rootfs_extract_debian_minimal();
            if (fs_resolve(img_root, &is_dir) < 0) {
                kprintf("appvm: rootfs extraction failed for '%s'\n", image);
                return -1;
            }
        } else if (strcmp(image, "android-stock") == 0 ||
                   strncmp(image, "android", 7) == 0) {
            kprintf("appvm: image '%s' not found, seeding android container...\n", image);
            rootfs_seed_android_stock();
            if (fs_resolve(img_root, &is_dir) < 0) {
                kprintf("appvm: android image seeding failed for '%s'\n", image);
                return -1;
            }
        } else {
            kprintf("appvm: image '%s' not found\n", image);
            return -1;
        }
    }

    container_t *c = &containers[slot];
    memset(c, 0, sizeof(container_t));
    c->id = container_next_id++;
    strncpy_safe(c->name, name, CONTAINER_NAME_MAX);
    strncpy_safe(c->image, image, IMAGE_NAME_MAX);
    /* Rootfs mounts are not available on every backend yet. Use the image
     * directory directly until the mount namespace backend can provide a
     * private bind-mounted root. */
    snprintf(c->root_path, sizeof(c->root_path), "%s", img_root);
    mkdirs(c->root_path);
    c->state = CONTAINER_CREATED;
    c->pid_1 = -1;
    c->ip = CONTAINER_IP_BASE + container_next_ip++;

    /* ─── Create isolated namespaces for this container ─── */
    char ns_name[CONTAINER_NAME_MAX + 16];

    snprintf(ns_name, sizeof(ns_name), "%s:mount", name);
    c->ns_mount = ns_create(NS_TYPE_MOUNT, ns_name);

    snprintf(ns_name, sizeof(ns_name), "%s:pid", name);
    c->ns_pid = ns_create(NS_TYPE_PID, ns_name);

    snprintf(ns_name, sizeof(ns_name), "%s:net", name);
    c->ns_net = ns_create(NS_TYPE_NETWORK, ns_name);

    snprintf(ns_name, sizeof(ns_name), "%s:user", name);
    c->ns_user = ns_create(NS_TYPE_USER, ns_name);

    snprintf(ns_name, sizeof(ns_name), "%s:uts", name);
    c->ns_uts = ns_create(NS_TYPE_UTS, ns_name);

    /* Set up network namespace with container IP */
    namespace_t *net_ns = ns_get_by_id(c->ns_net);
    if (net_ns) {
        net_ns->net_ip = c->ip;
        net_ns->net_mask = 0xFFFFFF00;
        net_ns->net_gateway = 0x0A000001;
    }

    /* Set up UTS namespace with container hostname */
    namespace_t *uts_ns = ns_get_by_id(c->ns_uts);
    if (uts_ns) {
        strlcpy(uts_ns->hostname, name, sizeof(uts_ns->hostname));
    }

    /* ─── Create a cgroup for this container ─── */
    char cg_name[CONTAINER_NAME_MAX + 8];
    snprintf(cg_name, sizeof(cg_name), "/%s", name);
    c->cgroup_id = cgroup_create(cg_name, 1);

    /* Set reasonable defaults */
    cgroup_set_memory_limit(c->cgroup_id, 256 * 1024 * 1024);  /* 256MB */
    cgroup_set_pids_max(c->cgroup_id, 64);
    cgroup_set_cpu_shares(c->cgroup_id, 1000);

    /* ─── Set up root filesystem mounts ─── */
    /* Mount /dev/null */
    char devnull_path[FS_PATH_MAX];
    snprintf(devnull_path, sizeof(devnull_path), "%s/dev/null", c->root_path);
    fs_mkfile(devnull_path);

    /* Mount /dev/zero */
    char devzero_path[FS_PATH_MAX];
    snprintf(devzero_path, sizeof(devzero_path), "%s/dev/zero", c->root_path);
    fs_mkfile(devzero_path);

    /* Mount /proc */
    char proc_path[FS_PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", c->root_path);
    mkdirs(proc_path);

    /* Register mount points in mount namespace */
    ns_mount(c->ns_mount, "devtmpfs", "/dev", "devtmpfs", 0, "");
    ns_mount(c->ns_mount, "proc", "/proc", "proc", 0, "");
    ns_mount(c->ns_mount, "sysfs", "/sys", "sysfs", 0, "");
    ns_mount(c->ns_mount, "tmpfs", "/tmp", "tmpfs", 0, "size=64M");
    ns_mount(c->ns_mount, c->root_path, "/", "bind", MS_BIND, "");

    container_log_append(c, "Container created with namespace isolation\n");
    kprintf("appvm: created container '%s' (id=%d) ns=[mount=%d pid=%d net=%d user=%d uts=%d] cg=%d\n",
            name, c->id, c->ns_mount, c->ns_pid, c->ns_net, c->ns_user, c->ns_uts, c->cgroup_id);
    return c->id;
}

/* ─── Add port mapping ─── */
int container_add_port(container_t *c, uint16_t host_port, uint16_t container_port, uint8_t proto) {
    if (c->port_count >= CONTAINER_MAX_PORTS) return -1;
    c->ports[c->port_count].host_port = host_port;
    c->ports[c->port_count].container_port = container_port;
    c->ports[c->port_count].protocol = proto;
    c->port_count++;
    return 0;
}

/* ─── Add volume mount ─── */
int container_add_volume(container_t *c, const char *host_path, const char *container_path, int ro) {
    if (c->volume_count >= CONTAINER_MAX_VOLUMES) return -1;
    strncpy_safe(c->volumes[c->volume_count].host_path, host_path, FS_PATH_MAX);
    strncpy_safe(c->volumes[c->volume_count].container_path, container_path, FS_PATH_MAX);
    c->volumes[c->volume_count].read_only = ro;
    c->volume_count++;

    /* Also register as bind mount in mount namespace */
    ns_mount(c->ns_mount, host_path, container_path, "bind", MS_BIND, ro ? "ro" : "");

    return 0;
}

/* ─── Start a container (launch PID 1) ─── */
int container_start(int id) {
    int saved_ns[PROC_NS_MAX];
    int saved_cg, saved_uid, saved_gid, saved_euid, saved_egid;
    container_t *c = container_get(id);
    if (!c) { kprintf("appvm: unknown container\n"); return -1; }
    if (c->state != CONTAINER_CREATED && c->state != CONTAINER_STOPPED) {
        kprintf("appvm: container '%s' already running\n", c->name);
        return -1;
    }

    /* Check cgroup limits before starting */
    if (cgroup_check_pids(c->cgroup_id) < 0) {
        kprintf("appvm: container '%s' exceeds PID limit\n", c->name);
        return -1;
    }

    char entrypoint[FS_PATH_MAX];
    snprintf(entrypoint, sizeof(entrypoint), "%s/sbin/init", c->root_path);

    int is_dir;
    if (fs_resolve(entrypoint, &is_dir) < 0) {
        snprintf(entrypoint, sizeof(entrypoint), "%s/init", c->root_path);
        if (fs_resolve(entrypoint, &is_dir) < 0) {
            snprintf(entrypoint, sizeof(entrypoint), "%s/bin/sh", c->root_path);
        }
    }
    if (fs_resolve(entrypoint, &is_dir) < 0) {
        /* The generated development rootfs has Debian metadata but no shell
         * binary. Run the CodeOS shell under the same namespaces so the mode
         * remains usable until a real Debian rootfs is installed. */
        if (strcmp(c->image, "debian-minimal") == 0) {
            strlcpy(entrypoint, "/bin/shell", sizeof(entrypoint));
            if (fs_resolve(entrypoint, &is_dir) < 0) {
                kprintf("appvm: no entrypoint found in container '%s'\n", c->name);
                return -1;
            }
            kprintf("appvm: using CodeOS shell for Debian development rootfs\n");
        } else {
            kprintf("appvm: no entrypoint found in container '%s'\n", c->name);
            return -1;
        }
    }

    char full_path[FS_PATH_MAX];
    strlcpy(full_path, entrypoint, sizeof(full_path));

    uint64_t entry, stack;
    elf_auxv_info_t auxv;
    if (elf_load(full_path, &entry, &stack, &auxv) < 0) {
        kprintf("appvm: failed to load entrypoint '%s'\n", full_path);
        return -1;
    }

    char *init_argv[] = { entrypoint, 0 };

    /* Prefer a dedicated process for pid-1 (fresh address space, safe from
     * any enclosing user session). Fall back to the shared-process path only
     * when called from inside a user syscall. */
    process_t *proc = proc_current();
    int host_mode = (proc != 0);
    uint64_t rsp;
    if (!proc) {
        rsp = elf_setup_stack(stack, entry, 1, init_argv, 0, 0, &auxv);
        if (!rsp) return -1;
        proc_create(full_path, entry, stack);
        proc = proc_current();
        if (!proc) return -1;
    } else {
        rsp = proc_exec(entry, stack, 1, init_argv, 0, &auxv);
        if (!rsp) return -1;
        for (int i = 0; i < PROC_NS_MAX; i++)
            saved_ns[i] = proc->namespaces[i];
        saved_cg = proc->cgroup_id;
        saved_uid = proc->uid; saved_gid = proc->gid;
        saved_euid = proc->euid; saved_egid = proc->egid;
    }

    if (proc) {
        if (c->process_count < CONTAINER_MAX_PROCS)
            c->pids[c->process_count++] = proc->pid;
        c->pid_1 = proc->pid;

        /* Assign process to container's namespaces */
        proc->namespaces[NS_TYPE_MOUNT] = c->ns_mount;
        proc->namespaces[NS_TYPE_PID] = c->ns_pid;
        proc->namespaces[NS_TYPE_NETWORK] = c->ns_net;
        proc->namespaces[NS_TYPE_USER] = c->ns_user;
        proc->namespaces[NS_TYPE_UTS] = c->ns_uts;

        /* Assign process to container's cgroup */
        proc->cgroup_id = c->cgroup_id;
        cgroup_add_proc(c->cgroup_id, proc->pid);

        /* Set UID/GID to root inside container */
        proc->uid = 0;
        proc->gid = 0;
        proc->euid = 0;
        proc->egid = 0;
    }
    c->state = CONTAINER_RUNNING;
    c->start_time = timer_get_milliseconds();

    /* Register PID in container's PID namespace */
    ns_add_proc(c->ns_pid, c->pid_1);

    char logmsg[96];
    snprintf(logmsg, sizeof(logmsg), "Container started (pid=%d, ns_isolated)\n", c->pid_1);
    container_log_append(c, logmsg);

    kprintf("appvm: started container '%s' (pid=%d, IP=%d.%d.%d.%d)\n",
            c->name, c->pid_1,
            (c->ip >> 24) & 0xFF, (c->ip >> 16) & 0xFF,
            (c->ip >> 8) & 0xFF, c->ip & 0xFF);

    extern uint64_t syscall_kernel_rsp;
    uint64_t outer_scrsp = syscall_kernel_rsp;
    uint64_t nest_phys = (uint64_t)pmm_alloc_page();
    uint64_t nest_scrsp = nest_phys ? (uint64_t)phys_to_virt(nest_phys) + 0x1000 : outer_scrsp;
    syscall_kernel_rsp = nest_scrsp;
    user_mode_preserve();
    user_mode_set_return(host_mode ? 0 : shell_exec_done);
    user_mode_begin();
    user_mode_enter(entry, rsp);
    if (!host_mode)
        return 0; /* control never resumes here in kernel-context mode */
    syscall_kernel_rsp = outer_scrsp;
    user_mode_restore();
    if (nest_phys) pmm_free_page(nest_phys);
    if (proc_current()) {
        for (int i = 0; i < PROC_NS_MAX; i++)
            proc_current()->namespaces[i] = saved_ns[i];
        proc_current()->cgroup_id = saved_cg;
        proc_current()->uid = saved_uid; proc_current()->gid = saved_gid;
        proc_current()->euid = saved_euid; proc_current()->egid = saved_egid;
    }
    return 0;
}

/* ─── Stop a container (kill all processes) ─── */
int container_stop(int id) {
    container_t *c = container_get(id);
    if (!c) { kprintf("appvm: unknown container\n"); return -1; }
    if (c->state != CONTAINER_RUNNING && c->state != CONTAINER_PAUSED) {
        kprintf("appvm: container '%s' is not running\n", c->name);
        return -1;
    }

    for (int i = 0; i < c->process_count; i++) {
        process_t *p = proc_get(c->pids[i]);
        if (p && p->state != PROC_ZOMBIE && p->state != PROC_DEAD) {
            /* Remove from cgroup */
            cgroup_remove_proc(c->cgroup_id, p->pid);
            /* Remove from PID namespace */
            ns_remove_proc(c->ns_pid, p->pid);
            p->state = PROC_ZOMBIE;
        }
    }
    c->state = CONTAINER_STOPPED;
    c->exit_code = 0;
    container_log_append(c, "Container stopped\n");
    kprintf("appvm: stopped container '%s'\n", c->name);
    return 0;
}

/* ─── Restart a container ─── */
int container_restart(int id) {
    if (container_stop(id) < 0) return -1;
    return container_start(id);
}

/* ─── Destroy a container (release namespaces + cgroups) ─── */
int container_destroy(int id) {
    container_t *c = container_get(id);
    if (!c) {
        for (int i = 0; i < CONTAINER_MAX; i++) {
            if (containers[i].id == id) c = &containers[i];
        }
    }
    if (!c && id > 0) {
        for (int i = 0; i < CONTAINER_MAX; i++) {
            if (containers[i].id == id) { c = &containers[i]; break; }
        }
    }
    if (!c) return -1;
    if (c->state == CONTAINER_RUNNING)
        container_stop(id);

    /* Release namespaces */
    if (c->ns_mount) ns_put(c->ns_mount);
    if (c->ns_pid) ns_put(c->ns_pid);
    if (c->ns_net) ns_put(c->ns_net);
    if (c->ns_user) ns_put(c->ns_user);
    if (c->ns_uts) ns_put(c->ns_uts);

    /* Destroy cgroup */
    if (c->cgroup_id) cgroup_destroy(c->cgroup_id);

    int saved_id = c->id;
    memset(c, 0, sizeof(container_t));
    kprintf("appvm: destroyed container id=%d (namespaces + cgroups released)\n", saved_id);
    return 0;
}

/* ─── List running containers ─── */
int container_list(char out[][CONTAINER_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < CONTAINER_MAX && count < max; i++) {
        if (containers[i].state == CONTAINER_RUNNING) {
            strncpy_safe(out[count], containers[i].name, CONTAINER_NAME_MAX);
            count++;
        }
    }
    return count;
}

/* ─── List all containers ─── */
int container_list_all(char out[][CONTAINER_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < CONTAINER_MAX && count < max; i++) {
        if (containers[i].id > 0) {
            strncpy_safe(out[count], containers[i].name, CONTAINER_NAME_MAX);
            count++;
        }
    }
    return count;
}

/* Android apps live under /system/app/<name>/<name> in the container image;
 * when exec'd they get the user-window bridge (fds 3/4 -> desktop windows). */
static int is_android_app_path(const char *path) {
    return path && strncmp(path, "/system/app/", 12) == 0;
}

/* ─── Execute inside a running container ─── */
/* Mark a freshly-created container as RUNNING without booting an
 * entrypoint, so a command can be exec'd directly (namespaces + cgroup
 * were set up by container_create). Used by `appvm run <img> <cmd>`. */
int container_mark_running(int id) {
    container_t *c = container_get(id);
    if (!c) return -1;
    if (c->state != CONTAINER_CREATED) return -1;
    c->state = CONTAINER_RUNNING;
    return 0;
}

int container_exec(int id, const char *path, int argc, char **argv, char **envp) {
    int saved_ns[PROC_NS_MAX];
    int saved_cg, saved_uid, saved_gid, saved_euid, saved_egid;
    container_t *c = container_get(id);
    if (!c) {
        kprintf("appvm: unknown container id %d\n", id);
        return -1;
    }
    if (c->state != CONTAINER_RUNNING) {
        kprintf("appvm: container '%s' is not running\n", c->name);
        return -1;
    }

    char full_path[FS_PATH_MAX];
    int rl = strlen(c->root_path);
    if (*path == '/') {
        memcpy(full_path, c->root_path, rl);
        strlcpy(full_path + rl, path, sizeof(full_path) - rl);
    } else {
        memcpy(full_path, c->root_path, rl);
        full_path[rl] = '/';
        strlcpy(full_path + rl + 1, path, sizeof(full_path) - rl - 1);
    }

    uint64_t entry = 0, stack = 0;
    elf_auxv_info_t auxv;
    if (elf_load(full_path, &entry, &stack, &auxv) < 0) {
        kprintf("appvm: exec '%s' failed (resolved to '%s')\n", path, full_path);
        return -1;
    }
    kprintf("appvm: exec '%s' in container '%s' (ns_isolated)\n", path, c->name);
    char logmsg[96];
    snprintf(logmsg, sizeof(logmsg), "exec: %s (ns_isolated)\n", path);
    container_log_append(c, logmsg);

    process_t *cur = proc_current();
    int host_mode = (cur != 0);
    uint64_t rsp;
    int envc = 0;
    if (envp) while (envp[envc] && envc < 64) envc++;
    if (!cur) {
        rsp = elf_setup_stack(stack, entry, argc, argv, envc, envp, &auxv);
        if (!rsp) return -1;
        proc_create(full_path, entry, stack);
        cur = proc_current();
        if (!cur) return -1;
    } else {
        rsp = proc_exec(entry, stack, argc, argv, envp, &auxv);
        if (!rsp) return -1;
        for (int i = 0; i < PROC_NS_MAX; i++)
            saved_ns[i] = cur->namespaces[i];
        saved_cg = cur->cgroup_id;
        saved_uid = cur->uid; saved_gid = cur->gid;
        saved_euid = cur->euid; saved_egid = cur->egid;
    }

    if (cur) {
        if (c->process_count < CONTAINER_MAX_PROCS)
            c->pids[c->process_count++] = cur->pid;

        /* Inherit container's namespace membership */
        cur->namespaces[NS_TYPE_MOUNT] = c->ns_mount;
        cur->namespaces[NS_TYPE_PID] = c->ns_pid;
        cur->namespaces[NS_TYPE_NETWORK] = c->ns_net;
        cur->namespaces[NS_TYPE_USER] = c->ns_user;
        cur->namespaces[NS_TYPE_UTS] = c->ns_uts;
        cur->cgroup_id = c->cgroup_id;
        cgroup_add_proc(c->cgroup_id, cur->pid);
        ns_add_proc(c->ns_pid, cur->pid);
    }

    /* Android apps draw through the user-window bridge (fds 3/4). */
    if (cur && is_android_app_path(path))
        user_wm_setup(cur->pid);

    extern uint64_t syscall_kernel_rsp;
    uint64_t outer_scrsp = syscall_kernel_rsp;
    uint64_t nest_phys = (uint64_t)pmm_alloc_page();
    uint64_t nest_scrsp = nest_phys ? (uint64_t)phys_to_virt(nest_phys) + 0x1000 : outer_scrsp;
    syscall_kernel_rsp = nest_scrsp;
    user_mode_preserve();
    user_mode_set_return(host_mode ? 0 : shell_exec_done);
    user_mode_begin();
    user_mode_enter(entry, rsp);
    if (is_android_app_path(path) && proc_current())
        user_wm_release(proc_current()->pid);
    if (!host_mode)
        return 0; /* control never resumes here in kernel-context mode */
    syscall_kernel_rsp = outer_scrsp;
    user_mode_restore();
    if (nest_phys) pmm_free_page(nest_phys);
    if (proc_current()) {
        for (int i = 0; i < PROC_NS_MAX; i++)
            proc_current()->namespaces[i] = saved_ns[i];
        proc_current()->cgroup_id = saved_cg;
        proc_current()->uid = saved_uid; proc_current()->gid = saved_gid;
        proc_current()->euid = saved_euid; proc_current()->egid = saved_egid;
    }
    return 0;
}

container_t *container_get_by_index(int idx) {
    if (idx < 0 || idx >= CONTAINER_MAX) return 0;
    return containers[idx].id > 0 ? &containers[idx] : 0;
}

int container_get_state(int id) {
    container_t *c = container_get(id);
    return c ? (int)c->state : -1;
}

void container_set_state(int id, container_state_t state) {
    container_t *c = container_get(id);
    if (c) c->state = state;
}

/* ─── Console I/O ring buffer helpers ─── */
#define CONSOLE_OUT_SIZE (64 * 1024)
#define CONSOLE_IN_SIZE  (4 * 1024)

void container_console_enable(int id) {
    container_t *c = container_get(id);
    if (c) c->console_active = 1;
}

int container_console_write(int id, const uint8_t *data, int len) {
    container_t *c = container_get(id);
    if (!c || !c->console_active) return -1;
    int written = 0;
    for (int i = 0; i < len; i++) {
        int next = (c->console_out_head + 1) % CONSOLE_OUT_SIZE;
        if (next == c->console_out_tail) break; /* full */
        c->console_out[c->console_out_head] = data[i];
        c->console_out_head = next;
        written++;
    }
    return written;
}

int container_console_read(int id, uint8_t *buf, int max) {
    container_t *c = container_get(id);
    if (!c) return -1;
    int read = 0;
    while (read < max && c->console_out_tail != c->console_out_head) {
        buf[read++] = c->console_out[c->console_out_tail];
        c->console_out_tail = (c->console_out_tail + 1) % CONSOLE_OUT_SIZE;
    }
    return read;
}

int container_stdin_write(int id, const uint8_t *data, int len) {
    container_t *c = container_get(id);
    if (!c || !c->console_active) return -1;
    int written = 0;
    for (int i = 0; i < len; i++) {
        int next = (c->console_in_head + 1) % CONSOLE_IN_SIZE;
        if (next == c->console_in_tail) break;
        c->console_in[c->console_in_head] = data[i];
        c->console_in_head = next;
        written++;
    }
    return written;
}

int container_stdin_read(int id, uint8_t *buf, int max) {
    container_t *c = container_get(id);
    if (!c) return -1;
    int read = 0;
    while (read < max && c->console_in_tail != c->console_in_head) {
        buf[read++] = c->console_in[c->console_in_tail];
        c->console_in_tail = (c->console_in_tail + 1) % CONSOLE_IN_SIZE;
    }
    return read;
}

/* ─── Resource stats ─── */
int container_stats(int id, uint64_t *mem, uint64_t *cpu) {
    container_t *c = container_get(id);
    if (!c) return -1;
    cgroup_t *cg = cgroup_get(c->cgroup_id);
    if (mem) *mem = cg ? cg->mem_usage : 0;
    if (cpu) *cpu = c->cpu_ticks;
    return 0;
}

/* ─── Get container resource limits ─── */
int container_get_limits(int id, uint64_t *mem_limit, int *cpu_shares, int *cpu_quota_us, int *pids_max) {
    container_t *c = container_get(id);
    if (!c) return -1;
    cgroup_t *cg = cgroup_get(c->cgroup_id);
    if (cg) {
        if (mem_limit) *mem_limit = cg->mem_limit;
        if (cpu_shares) *cpu_shares = cg->cpu_shares;
        if (cpu_quota_us) *cpu_quota_us = cg->cpu_quota_us;
        if (pids_max) *pids_max = cg->pids_max;
    }
    return 0;
}

/* ─── Read container logs ─── */
int container_logs(int id, char *buf, int max) {
    container_t *c = container_get(id);
    if (!c) return -1;
    int copied = 0;
    int start = c->log_count < CONTAINER_LOG_SIZE
        ? 0
        : c->log_head;
    for (int i = 0; i < c->log_count && copied < max - 1; i++) {
        int idx = (start + i) % CONTAINER_LOG_SIZE;
        buf[copied++] = c->log[idx];
    }
    buf[copied] = 0;
    return copied;
}

/* ─── Inspect container details (with namespace + cgroup info) ─── */
int container_inspect(int id, char *buf, int max) {
    container_t *c = container_get(id);
    if (!c) return -1;
    int n = 0;
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "Container: %s\n", c->name);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  ID:       %d\n", c->id);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Image:    %s\n", c->image);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Root:     %s\n", c->root_path);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  State:    %s\n",
        c->state == CONTAINER_CREATED ? "created" :
        c->state == CONTAINER_RUNNING ? "running" :
        c->state == CONTAINER_PAUSED  ? "paused"  : "stopped");
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  PID 1:    %d\n", c->pid_1);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Procs:    %d\n", c->process_count);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  IP:       %d.%d.%d.%d\n",
        (c->ip >> 24) & 0xFF, (c->ip >> 16) & 0xFF,
        (c->ip >> 8) & 0xFF, c->ip & 0xFF);

    /* Namespace info */
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Namespaces:\n");
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    Mount:  %d\n", c->ns_mount);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    PID:    %d\n", c->ns_pid);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    Net:    %d\n", c->ns_net);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    User:   %d\n", c->ns_user);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    UTS:    %d\n", c->ns_uts);

    /* Mount points */
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Mounts:   %d\n",
                  ns_mount_count(c->ns_mount));

    /* Cgroup info */
    cgroup_t *cg = cgroup_get(c->cgroup_id);
    if (cg) {
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Cgroup:   %s (id=%d)\n", cg->name, cg->id);
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    CPU shares:   %d\n", cg->cpu_shares);
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    CPU quota:    %d us / %d us\n",
                      cg->cpu_quota_us, cg->cpu_period_us);
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    Mem limit:    %s\n",
                      cg->mem_limit ? "(set)" : "unlimited");
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    Mem usage:    %lu bytes\n", cg->mem_usage);
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "    PIDs:         %d / %s\n",
                      cg->pids_current,
                      cg->pids_max < 0 ? "unlimited" : "(set)");
    }

    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Ports:    ");
    if (c->port_count == 0) {
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "(none)\n");
    } else {
        for (int i = 0; i < c->port_count; i++)
            n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "%d->%d/%s ",
                c->ports[i].host_port, c->ports[i].container_port,
                c->ports[i].protocol == 6 ? "tcp" : "udp");
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "\n");
    }
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Volumes:  ");
    if (c->volume_count == 0) {
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "(none)\n");
    } else {
        for (int i = 0; i < c->volume_count; i++)
            n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "%s:%s%s ",
                c->volumes[i].host_path, c->volumes[i].container_path,
                c->volumes[i].read_only ? ":ro" : "");
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "\n");
    }
    uint64_t elapsed = 0;
    if (c->start_time)
        elapsed = timer_get_milliseconds() - c->start_time;
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Uptime:   %lu ms\n", elapsed);
    return n;
}

/* ─── Launch a container with console I/O bridge (fork-based) ─── */
/* Forks a child that runs a kernel shell loop, bridging stdin/stdout
 * ring buffers to shell_execute(). Output is captured via kprintf_capture. */
int container_launch_console(int id) {
    container_t *c = container_get(id);
    if (!c) { kprintf("appvm: container_launch_console: unknown id %d\n", id); return -1; }
    if (c->state != CONTAINER_CREATED && c->state != CONTAINER_STOPPED) {
        kprintf("appvm: container_launch_console: '%s' already running\n", c->name);
        return -1;
    }

    /* Auto-extract rootfs if the image directory is missing */
    {
        int is_dir;
        char img_root[FS_PATH_MAX];
        snprintf(img_root, sizeof(img_root), "/containers/images/%s", c->image);
        if (fs_resolve(img_root, &is_dir) < 0) {
            if (strcmp(c->image, "debian-minimal") == 0) {
                kprintf("appvm: auto-extracting debian-minimal rootfs for '%s'\n", c->name);
                rootfs_extract_debian_minimal();
            }
        }
    }

    /* Enable console I/O on this container */
    c->console_active = 1;
    c->console_out_head = 0;
    c->console_out_tail = 0;
    c->console_in_head = 0;
    c->console_in_tail = 0;

    /* Fork: child becomes the container shell loop, parent returns child PID */
    int fork_pid = proc_fork();
    if (fork_pid < 0) {
        kprintf("appvm: fork failed for container '%s'\n", c->name);
        return -1;
    }

    if (fork_pid == 0) {
        /* ── CHILD: kernel shell loop bridging ring buffers ── */
        process_t *proc = proc_current();
        if (proc) {
            if (c->process_count < CONTAINER_MAX_PROCS)
                c->pids[c->process_count++] = proc->pid;
            c->pid_1 = proc->pid;

            proc->namespaces[NS_TYPE_MOUNT] = c->ns_mount;
            proc->namespaces[NS_TYPE_PID] = c->ns_pid;
            proc->namespaces[NS_TYPE_NETWORK] = c->ns_net;
            proc->namespaces[NS_TYPE_USER] = c->ns_user;
            proc->namespaces[NS_TYPE_UTS] = c->ns_uts;
            proc->cgroup_id = c->cgroup_id;
            cgroup_add_proc(c->cgroup_id, proc->pid);
            ns_add_proc(c->ns_pid, proc->pid);

            proc->uid = 0;
            proc->gid = 0;
            proc->euid = 0;
            proc->egid = 0;
        }

        c->state = CONTAINER_RUNNING;
        c->start_time = timer_get_milliseconds();

        kprintf("appvm: console shell loop started for '%s' (pid=%d)\n",
                c->name, proc ? proc->pid : -1);

        /* Send welcome banner to the console ring buffer */
        const char *banner =
            "Debian GNU/Linux 8 (jessie) (CodeOS Linux Terminal)\n"
            "\n"
            " * Type 'help' for a list of commands\n"
            " * Type 'exit' to close the terminal\n"
            "\n";
        container_console_write(id, (const uint8_t *)banner, (int)strlen(banner));

        /* ── Main shell loop ── */
        int running = 1;
        while (running) {
            /* Write prompt */
            const char *prompt = "root# ";
            container_console_write(id, (const uint8_t *)prompt, (int)strlen(prompt));

            /* Read a line from stdin ring buffer */
            char line[512];
            int line_pos = 0;
            line[0] = 0;

            while (1) {
                uint8_t ch;
                int n = container_stdin_read(id, &ch, 1);
                if (n <= 0) {
                    sched_yield();
                    continue;
                }

                if (ch == '\n' || ch == '\r') {
                    container_console_write(id, (const uint8_t *)"\r\n", 2);
                    break;
                } else if (ch == '\b' || ch == 0x7F) {
                    if (line_pos > 0) {
                        line_pos--;
                        line[line_pos] = 0;
                        container_console_write(id, (const uint8_t *)"\b \b", 3);
                    }
                } else if (ch == 3) {
                    line_pos = 0;
                    line[0] = 0;
                    container_console_write(id, (const uint8_t *)"^C\r\n", 4);
                    break;
                } else if (ch == 4) {
                    running = 0;
                    break;
                } else if (ch >= 32 && line_pos < (int)sizeof(line) - 1) {
                    line[line_pos++] = ch;
                    line[line_pos] = 0;
                    container_console_write(id, &ch, 1);
                }
            }

            if (!running) break;
            if (line_pos == 0) continue;
            if (strcmp(line, "exit") == 0) break;

            /* Execute via kernel shell with output capture */
            kprintf_capture_begin();
            shell_execute(line);
            int cap_len = kprintf_capture_end();

            if (cap_len > 0) {
                const char *cap = kprintf_capture_get();
                container_console_write(id, (const uint8_t *)cap, cap_len);
                if (cap[cap_len - 1] != '\n')
                    container_console_write(id, (const uint8_t *)"\r\n", 2);
            }
        }

        const char *bye = "\n[Process completed]\n";
        container_console_write(id, (const uint8_t *)bye, (int)strlen(bye));

        c->state = CONTAINER_STOPPED;
        c->exit_code = 0;
        container_log_append(c, "Console shell exited\n");
        kprintf("appvm: console shell loop exited for '%s'\n", c->name);
        return 0;
    }

    /* ── PARENT: return child PID ── */
    kprintf("appvm: container '%s' launched (child_pid=%d)\n", c->name, fork_pid);
    return fork_pid;
}
