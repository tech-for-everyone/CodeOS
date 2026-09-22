#include "vm_manager.h"
#include "namespace.h"
#include "kprintf.h"
#include "string.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "fs.h"
#include "elf.h"
#include "umode.h"
#include "signal.h"
#include "vmm.h"

static vm_t vms[VM_MAX];
static int vm_next_id = 1;

/* ─── Default Linux rootfs paths ─── */
#define LINUX_ROOTFS_DEFAULT "/containers/images/linux-rootfs"
#define ANDROID_ROOTFS_DEFAULT "/containers/images/android-rootfs"

/* ─── crosvm support ─── */
#define CROSVM_BINARY "/usr/bin/crosvm"
#define CROSVM_DEFAULT_KERNEL "/boot/vmlinux"
#define CROSVM_DEFAULT_INITRD "/boot/initrd.img"
#define CROSVM_SOCKET_DIR "/run/crosvm"

/* Virtio device configuration */
typedef struct {
    int enabled;
    char path[VM_IMAGE_PATH_MAX];
    int readonly;
} virtio_blk_config_t;

typedef struct {
    int enabled;
    char mac[18];
    char tap_name[16];
    int mtu;
} virtio_net_config_t;

typedef struct {
    int enabled;
    int width;
    int height;
    int bpp;
} virtio_gpu_config_t;

typedef struct {
    int enabled;
    int num_devices;
} virtio_balloon_config_t;

typedef struct {
    int enabled;
    char socket_path[VM_IMAGE_PATH_MAX];
} virtio_console_config_t;

static int crosvm_build_cmd(vm_t *vm, char *out_cmd, size_t out_size) {
    if (!vm->config.kernel_path[0]) {
        strlcpy(vm->config.kernel_path, CROSVM_DEFAULT_KERNEL, VM_IMAGE_PATH_MAX);
    }
    if (!vm->config.init_path[0]) {
        strlcpy(vm->config.init_path, CROSVM_DEFAULT_INITRD, VM_IMAGE_PATH_MAX);
    }

    char cmd[4096];
    int n = 0;
    n += snprintf(cmd + n, sizeof(cmd) - n,
                  "%s run --disable-sandbox --cpus %d --memory %uM",
                  CROSVM_BINARY, vm->config.cpu_count > 0 ? vm->config.cpu_count : 2,
                  vm->config.memory_mb > 0 ? vm->config.memory_mb : 1024);

    if (vm->config.kernel_path[0]) {
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " --kernel %s", vm->config.kernel_path);
    }
    if (vm->config.init_path[0]) {
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " --initrd %s", vm->config.init_path);
    }

    /* Root filesystem - virtio-blk */
    if (vm->config.rootfs_path[0]) {
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " -r %s", vm->config.rootfs_path);
    } else {
        /* Fallback: use the VM name as a disk image */
        char disk_path[VM_IMAGE_PATH_MAX];
        snprintf(disk_path, sizeof(disk_path), "/var/lib/crosvm/%s/disk.img", vm->name);
        n += snprintf(cmd + n, sizeof(cmd) - n, " -r %s", disk_path);
    }

    /* Control socket — lifecycle (stop/suspend/resume/snapshot) without
       signals.  The userspace launcher drives it via .ctl requests. */
    char ctrl_socket[VM_IMAGE_PATH_MAX];
    snprintf(ctrl_socket, sizeof(ctrl_socket), "%s/%s.sock", CROSVM_SOCKET_DIR, vm->name);
    n += snprintf(cmd + n, sizeof(cmd) - n,
                  " --control-socket %s", ctrl_socket);

    /* Serial console - stdio for interactive, file for logging */
    char log_path[VM_IMAGE_PATH_MAX];
    snprintf(log_path, sizeof(log_path), "/var/log/crosvm/%s.log", vm->name);
    n += snprintf(cmd + n, sizeof(cmd) - n,
                  " --serial type=stdio --serial type=file,path=%s", log_path);

    /* Virtio console for guest communication */
    char console_socket[VM_IMAGE_PATH_MAX];
    snprintf(console_socket, sizeof(console_socket), "%s/%s-console.sock", CROSVM_SOCKET_DIR, vm->name);
    n += snprintf(cmd + n, sizeof(cmd) - n,
                  " --serial type=socket,path=%s,mode=server", console_socket);

    /* Virtio network */
    if (vm->config.enable_network) {
        char tap_name[16];
        snprintf(tap_name, sizeof(tap_name), "crosvm%d", vm->id);
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " --net tap-name=%s,mac=52:54:00:%02x:%02x:%02x",
                      tap_name,
                      (vm->id >> 16) & 0xff, (vm->id >> 8) & 0xff, vm->id & 0xff);
    }

    /* Virtio GPU */
    if (vm->config.enable_gpu) {
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " --gpu width=%d,height=%d,bpp=%d",
                      vm->display_width, vm->display_height, vm->display_bpp);
    }

    /* Virtio balloon for memory management */
    n += snprintf(cmd + n, sizeof(cmd) - n, " --balloon");

    /* 9p shared directory — correct --fs syntax */
    if (vm->config.enable_9p && vm->config.shared_path[0]) {
        char mount_tag[64];
        snprintf(mount_tag, sizeof(mount_tag), "shared-%d", vm->id);
        n += snprintf(cmd + n, sizeof(cmd) - n,
                      " --fs source=%s,tag=%s",
                      vm->config.shared_path, mount_tag);
    }

    /* RNG device */
    n += snprintf(cmd + n, sizeof(cmd) - n, " --rng");

    /* VM name */
    n += snprintf(cmd + n, sizeof(cmd) - n, " %s", vm->name);

    strlcpy(out_cmd, cmd, out_size);
    return 0;
}

/* Filesystem-based crosvm command storage for userspace launcher */
#define CROSVM_CMD_DIR "/tmp/crosvm-cmds"

static int crosvm_store_cmd(vm_t *vm) {
    char cmd[4096];
    if (crosvm_build_cmd(vm, cmd, sizeof(cmd)) < 0) return -1;

    fs_mkdir(CROSVM_CMD_DIR);

    char cmd_file[256];
    snprintf(cmd_file, sizeof(cmd_file), "%s/%s.cmd", CROSVM_CMD_DIR, vm->name);
    int fd = fs_mkfile(cmd_file);
    if (fd < 0) return -1;
    fs_write(cmd_file, cmd, strlen(cmd));
    return 0;
}

/* Write a control directive (stop/pause/resume) that the launcher applies */
static int crosvm_write_ctl(vm_t *vm, const char *ctl) {
    fs_mkdir(CROSVM_CMD_DIR);
    char ctl_file[256];
    snprintf(ctl_file, sizeof(ctl_file), "%s/%s.ctl", CROSVM_CMD_DIR, vm->name);
    int fd = fs_mkfile(ctl_file);
    if (fd < 0) return -1;
    fs_write(ctl_file, ctl, strlen(ctl));
    return 0;
}

static int crosvm_remove_cmd(vm_t *vm) {
    char cmd_file[256];
    snprintf(cmd_file, sizeof(cmd_file), "%s/%s.cmd", CROSVM_CMD_DIR, vm->name);
    fs_rm(cmd_file);
    return 0;
}

static int crosvm_start_vm(vm_t *vm) {
    if (!vm->config.kernel_path[0]) {
        strlcpy(vm->config.kernel_path, CROSVM_DEFAULT_KERNEL, VM_IMAGE_PATH_MAX);
    }
    if (!vm->config.init_path[0]) {
        strlcpy(vm->config.init_path, CROSVM_DEFAULT_INITRD, VM_IMAGE_PATH_MAX);
    }

    /* crosvm needs these to exist before it can run.  fs_mkdir fails on
       existing dirs and has no mkdir -p semantics, so create every level. */
    fs_mkdir("/tmp");
    fs_mkdir(CROSVM_CMD_DIR);
    fs_mkdir("/run");
    fs_mkdir(CROSVM_SOCKET_DIR);
    fs_mkdir("/var");
    fs_mkdir("/var/log");
    fs_mkdir("/var/log/crosvm");
    fs_mkdir("/var/lib");
    fs_mkdir("/var/lib/crosvm");

    char vm_dir[VM_IMAGE_PATH_MAX];
    snprintf(vm_dir, sizeof(vm_dir), "/var/lib/crosvm/%s", vm->name);
    fs_mkdir(vm_dir);
    char snap_dir[VM_IMAGE_PATH_MAX];
    snprintf(snap_dir, sizeof(snap_dir), "%s/snapshots", vm_dir);
    fs_mkdir(snap_dir);

    if (crosvm_store_cmd(vm) < 0) return -1;

    /* Clear stale feedback from any previous run of this VM name */
    char fb[256];
    snprintf(fb, sizeof(fb), "%s/%s.exit", CROSVM_CMD_DIR, vm->name);
    fs_rm(fb);
    snprintf(fb, sizeof(fb), "%s/%s.pid", CROSVM_CMD_DIR, vm->name);
    fs_rm(fb);

    vm->config.enable_gpu = 1;
    if (vm_daemon_start() < 0) {
        kprintf("crosvm: failed to start launcher daemon\n");
        return -1;
    }
    kprintf("crosvm: VM '%s' staged for launch\n", vm->name);
    /* State stays BOOTING until vm_sync_states() observes <name>.pid */
    vm->start_time = timer_get_milliseconds();
    return 0;
}

static int crosvm_stop_vm(vm_t *vm) {
    crosvm_remove_cmd(vm);
    if (vm->container_id > 0)
        crosvm_write_ctl(vm, "stop");
    /* Stay STOPPING until vm_sync_states() observes <name>.exit. */
    vm->state = VM_STATE_STOPPING;
    return 0;
}

static int crosvm_pause_vm(vm_t *vm) {
    crosvm_write_ctl(vm, "pause");
    vm->state = VM_STATE_PAUSED;
    return 0;
}

static int crosvm_resume_vm(vm_t *vm) {
    crosvm_write_ctl(vm, "resume");
    vm->state = VM_STATE_RUNNING;
    return 0;
}

static int crosvm_snapshot_vm(vm_t *vm, const char *snapshot_name) {
    if (!snapshot_name || !snapshot_name[0]) return -1;

    /* Create snapshot directory */
    char snap_dir[VM_IMAGE_PATH_MAX];
    snprintf(snap_dir, sizeof(snap_dir), "/var/lib/crosvm/%s/snapshots", vm->name);
    fs_mkdir(snap_dir);

    /* Write snapshot command via filesystem IPC */
    char ctl_file[256];
    snprintf(ctl_file, sizeof(ctl_file), "%s/%s.ctl", CROSVM_CMD_DIR, vm->name);
    int fd = fs_mkfile(ctl_file);
    if (fd < 0) return -1;

    char snap_cmd[256];
    snprintf(snap_cmd, sizeof(snap_cmd), "snapshot %s", snapshot_name);
    fs_write(ctl_file, snap_cmd, strlen(snap_cmd));

    kprintf("crosvm: snapshot '%s' requested for VM '%s'\n", snapshot_name, vm->name);
    return 0;
}

/* ─── Launcher daemon (kernel-side) ──────────────────────────────────────
 * Polls /tmp/crosvm-cmds the same way the userspace launcher daemon would,
 * but as a kernel thread: no user-mode transition, no scheduler hazards.
 *
 *   <name>.cmd          — stage + try to exec the VM process
 *   <name>.ctl          — stop/pause/resume/snapshot <name> directives
 *   <name>.pid / .exit  — feedback consumed by vm_sync_states()
 *
 * The crosvm binary itself does not ship with the OS yet; when absent the
 * staged VM is failed honestly (exit 127 -> CRASHED) so callers see the
 * true lifecycle instead of a stuck BOOTING state. */
#define VMD_POLL_MS 200

static int vmd_write_feedback(const char *name, const char *suffix,
                              const char *text) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s.%s", CROSVM_CMD_DIR, name, suffix);
    fs_rm(path);                 /* replace any previous feedback */
    if (fs_mkfile(path) < 0) return -1;
    return fs_write(path, text, strlen(text)) < 0 ? -1 : 0;
}

static void vmd_handle_cmd_file(const char *entry, int elen) {
    char name[VM_NAME_MAX];
    int vlen = elen - 4 > VM_NAME_MAX - 1 ? VM_NAME_MAX - 1 : elen - 4;
    memcpy(name, entry, vlen); name[vlen] = 0;

    char cmd_path[160];
    snprintf(cmd_path, sizeof(cmd_path), "%s/%s", CROSVM_CMD_DIR, entry);

    char cmd[512];
    int n = fs_read(cmd_path, cmd, sizeof(cmd) - 1);
    fs_rm(cmd_path);
    if (n <= 0) return;
    cmd[n] = 0;
    kprintf("crosvm: exec request '%s': %s\n", name, cmd);

    uint64_t bin_entry, bin_stack;
    elf_auxv_info_t auxv;
    if (elf_load(CROSVM_BINARY, &bin_entry, &bin_stack, &auxv) == 0) {
        /* Binary present: a future umode exec path would start here.
           Until then, refuse rather than half-launch. */
        kprintf("crosvm: '%s' present but direct exec is not wired\n",
                CROSVM_BINARY);
        vmd_write_feedback(name, "exit", "126\n");
        return;
    }
    kprintf("crosvm: %s not available -- failing VM '%s'\n",
            CROSVM_BINARY, name);
    vmd_write_feedback(name, "exit", "127\n");
}

static void vmd_handle_ctl_file(const char *entry, int elen) {
    char name[VM_NAME_MAX];
    int vlen = elen - 4 > VM_NAME_MAX - 1 ? VM_NAME_MAX - 1 : elen - 4;
    memcpy(name, entry, vlen); name[vlen] = 0;

    char ctl_path[160];
    snprintf(ctl_path, sizeof(ctl_path), "%s/%s", CROSVM_CMD_DIR, entry);

    char ctl[64];
    int n = fs_read(ctl_path, ctl, sizeof(ctl) - 1);
    fs_rm(ctl_path);
    if (n <= 0) return;
    while (n > 0 && (ctl[n-1] == '\n' || ctl[n-1] == ' ')) ctl[--n] = 0;

    vm_t *vm = vm_find(name);
    if (!vm || vm->container_id <= 0) {
        if (strncmp(ctl, "snapshot", 8) != 0)
            kprintf("crosvm: '%s' not running, ignoring '%s'\n", name, ctl);
        return;
    }

    int pid = vm->container_id;
    if (strcmp(ctl, "stop") == 0) {
        proc_kill(pid, SIGTERM);
        vm->state = VM_STATE_STOPPING;
    } else if (strcmp(ctl, "pause") == 0) {
        proc_kill(pid, SIGSTOP);
        vm->state = VM_STATE_PAUSED;
    } else if (strcmp(ctl, "resume") == 0) {
        proc_kill(pid, SIGCONT);
        vm->state = VM_STATE_RUNNING;
    } else if (strncmp(ctl, "snapshot", 8) == 0) {
        const char *snap = ctl[8] == ' ' ? ctl + 9 : "";
        char dir[160];
        if (!snap[0]) snap = "default";
        snprintf(dir, sizeof(dir), "/var/lib/crosvm/%s/snapshots/%s", name, snap);
        fs_mkdir("/var/lib");
        fs_mkdir("/var/lib/crosvm");
        char parent[128];
        snprintf(parent, sizeof(parent), "/var/lib/crosvm/%s/snapshots", name);
        fs_mkdir(parent);
        fs_mkdir(dir);
        kprintf("crosvm: snapshot '%s' of '%s' -> %s (via control socket when available)\n",
                snap, name, dir);
    }
}

static int vmd_dir_scan(char *buf, int max) {
    return fs_dir_list(CROSVM_CMD_DIR, buf, max);
}

static void vmd_poll_once(void) {
    static char names[2048];
    int n = vmd_dir_scan(names, sizeof(names));
    if (n <= 0) return;

    int off = 0;
    while (off < n) {
        const char *entry = names + off;
        int elen = 0;
        while (off < n && names[off]) { off++; elen++; }
        off++;  /* skip NUL */
        if (elen <= 2 || elen > (int)sizeof(((vm_t *)0)->name) + 8) continue;
        if (elen > 4 && strncmp(entry + elen - 4, ".cmd", 4) == 0)
            vmd_handle_cmd_file(entry, elen);
        else if (elen > 4 && strncmp(entry + elen - 4, ".ctl", 4) == 0)
            vmd_handle_ctl_file(entry, elen);
    }
}

static void vmd_thread_main(void) {
    for (;;) {
        sched_sleep_ms(VMD_POLL_MS);
        vmd_poll_once();
    }
}

int vm_daemon_start(void) {
    static int vmd_started = 0;
    if (vmd_started) return 0;
    int tid = sched_create_thread("crosvm-vmd", vmd_thread_main);
    if (tid < 0) return -1;
    vmd_started = 1;
    return 0;
}

/* ─── Resource monitoring ─── */

int vm_get_memory_usage(int vm_id, uint64_t *rss_bytes) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    if (vm->type != VM_TYPE_CROSVM) return -1;
    if (vm->state != VM_STATE_RUNNING) return -1;

    /* For crosvm VMs, container_id holds the PID */
    int pid = vm->container_id;
    if (pid <= 0) return -1;

    /* Read /proc/{pid}/status to get VmRSS */
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    char buf[4096];
    int n = fs_read(path, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Parse VmRSS line */
    const char *rss_marker = "VmRSS:";
    for (int i = 0; i < n - 7; i++) {
        if (buf[i] == 'V' && buf[i+1] == 'm' && buf[i+2] == 'R' &&
            buf[i+3] == 'S' && buf[i+4] == 'S' && buf[i+5] == ':') {
            /* Skip whitespace after VmRSS: */
            int j = i + 6;
            while (j < n && (buf[j] == ' ' || buf[j] == '\t')) j++;
            /* Parse number */
            uint64_t val = 0;
            while (j < n && buf[j] >= '0' && buf[j] <= '9') {
                val = val * 10 + (buf[j] - '0');
                j++;
            }
            /* Skip " kB" suffix, convert to bytes */
            if (rss_bytes) *rss_bytes = val * 1024;
            return 0;
        }
    }
    return -1;
}

int vm_get_cpu_usage(int vm_id, uint64_t *cpu_ticks) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    if (vm->type != VM_TYPE_CROSVM) return -1;
    if (vm->state != VM_STATE_RUNNING) return -1;

    int pid = vm->container_id;
    if (pid <= 0) return -1;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    char buf[1024];
    int n = fs_read(path, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Parse utime + stime from /proc/pid/stat
     * Format: pid (comm) state ppid ... utime(14) stime(15) */
    int fields = 0;
    uint64_t utime = 0, stime = 0;
    const char *p = buf;
    /* Skip past the comm field (may contain spaces) */
    while (*p && *p != ')') p++;
    if (*p == ')') p++;
    p++; /* skip space after ) */
    for (int i = 3; i <= 15 && *p; i++) {
        while (*p == ' ') p++;
        uint64_t val = 0;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        while (*p && *p != ' ') p++;
        if (i == 14) utime = val;
        if (i == 15) stime = val;
    }
    if (cpu_ticks) *cpu_ticks = utime + stime;
    return 0;
}

int vm_manager_init(void) {
    memset(vms, 0, sizeof(vms));
    kprintf("vm: VM manager ready (%d slots)\n", VM_MAX);
    return 0;
}

/* ─── Launcher feedback sync ─────────────────────────────────────────────
 * The userspace crosvm-launcher publishes:
 *   <name>.pid   — real VM pid once forked (kernel consumes, sets RUNNING)
 *   <name>.exit  — process exit status when the VM ends (kernel consumes)
 * Call this before any state-dependent operation so states reflect reality. */
void vm_sync_states(void) {
    for (int i = 0; i < VM_MAX; i++) {
        vm_t *vm = &vms[i];
        if (vm->id == 0 || vm->type != VM_TYPE_CROSVM) continue;

        char path[128];
        char buf[32];

        if (vm->state == VM_STATE_BOOTING || vm->state == VM_STATE_STOPPING) {
            snprintf(path, sizeof(path), "%s/%s.exit", CROSVM_CMD_DIR, vm->name);
            int n = fs_read(path, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                int code = 0;
                for (const char *c = buf; *c >= '0' && *c <= '9'; c++)
                    code = code * 10 + (*c - '0');
                vm->exit_code = code;
                vm->stop_time = timer_get_milliseconds();
                vm->state = (code == 0) ? VM_STATE_STOPPED : VM_STATE_CRASHED;
                vm->container_id = 0;
                fs_rm(path);
                kprintf("vm: '%s' exited (status %d) -> %s\n", vm->name, code,
                        vm_state_str(vm->state));
                continue;
            }
        }

        if (vm->state == VM_STATE_BOOTING && vm->container_id <= 0) {
            snprintf(path, sizeof(path), "%s/%s.pid", CROSVM_CMD_DIR, vm->name);
            int n = fs_read(path, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                int pid = 0;
                for (const char *c = buf; *c >= '0' && *c <= '9'; c++)
                    pid = pid * 10 + (*c - '0');
                if (pid > 0) {
                    vm->container_id = pid;   /* doubles as the VM pid */
                    vm->state = VM_STATE_RUNNING;
                    kprintf("vm: '%s' running (pid %d)\n", vm->name, pid);
                }
                fs_rm(path);
            }
        }
    }
}

/* ─── Shared type label ─── */
const char *vm_type_str(vm_type_t type) {
    switch (type) {
        case VM_TYPE_LINUX_CONTAINER: return "linux";
        case VM_TYPE_ANDROID_CONTAINER: return "android";
        case VM_TYPE_CROSVM: return "crosvm";
        case VM_TYPE_PLUGIN: return "plugin";
    }
    return "unknown";
}

static int alloc_vm_slot(void) {
    for (int i = 0; i < VM_MAX; i++)
        if (vms[i].id == 0) return i;
    return -1;
}

const char *vm_state_str(vm_state_t state) {
    switch (state) {
        case VM_STATE_CREATED:  return "created";
        case VM_STATE_BOOTING:  return "booting";
        case VM_STATE_RUNNING:  return "running";
        case VM_STATE_PAUSED:   return "paused";
        case VM_STATE_STOPPING: return "stopping";
        case VM_STATE_STOPPED:  return "stopped";
        case VM_STATE_CRASHED:  return "crashed";
    }
    return "unknown";
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VM lifecycle
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_create(const vm_config_t *config) {
    if (!config || !config->name[0]) return -1;

    /* Check for duplicate name */
    if (vm_find(config->name)) {
        kprintf("vm: VM '%s' already exists\n", config->name);
        return -1;
    }

    int slot = alloc_vm_slot();
    if (slot < 0) {
        kprintf("vm: no slots available\n");
        return -1;
    }

    vm_t *vm = &vms[slot];
    memset(vm, 0, sizeof(vm_t));
    vm->id = vm_next_id++;
    strlcpy(vm->name, config->name, VM_NAME_MAX);
    vm->state = VM_STATE_CREATED;
    vm->type = config->type;
    memcpy(&vm->config, config, sizeof(vm_config_t));

    /* Create container for this VM — crosvm VMs are plain processes driven
       through filesystem IPC with the userspace launcher, no container. */
    if (config->type != VM_TYPE_CROSVM) {
        char container_name[VM_NAME_MAX + 8];
        snprintf(container_name, sizeof(container_name), "vm-%s", config->name);

        const char *image = "linux-root";
        if (config->type == VM_TYPE_ANDROID_CONTAINER)
            image = "android-root";

        vm->container_id = container_create(container_name, image);
        if (vm->container_id < 0) {
            kprintf("vm: failed to create container for VM '%s'\n", config->name);
            memset(vm, 0, sizeof(vm_t));
            return -1;
        }

        /* Configure container volumes from VM config */
    if (config->rootfs_path[0]) {
        container_t *c = container_get(vm->container_id);
        if (c) {
            container_add_volume(c, config->rootfs_path, "/", 0);
        }
    }

    if (config->enable_9p && config->shared_path[0]) {
        container_t *c = container_get(vm->container_id);
        if (c) {
            container_add_volume(c, config->shared_path,
                                config->shared_mount[0] ? config->shared_mount : "/mnt/shared",
                                0);
        }
    }
    }

    /* Allocate display buffer for VM */
    vm->display_width = 1920;
    vm->display_height = 1080;
    vm->display_bpp = 32;

    kprintf("vm: created VM '%s' (id=%d, type=%s, container=%d)\n",
            vm->name, vm->id, vm_type_str(vm->type), vm->container_id);
    return vm->id;
}

int vm_start(int vm_id) {
    vm_sync_states();
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;

    if (vm->state != VM_STATE_CREATED && vm->state != VM_STATE_STOPPED &&
        vm->state != VM_STATE_CRASHED) {
        kprintf("vm: VM '%s' is in state %s, cannot start\n",
                vm->name, vm_state_str(vm->state));
        return -1;
    }

    vm->state = VM_STATE_BOOTING;
    vm->start_time = timer_get_milliseconds();

    int ret = -1;
    if (vm->type == VM_TYPE_CROSVM) {
        ret = crosvm_start_vm(vm);
    } else {
        /* Start the underlying container */
        if (container_start(vm->container_id) < 0) {
            kprintf("vm: failed to start container for VM '%s'\n", vm->name);
            vm->state = VM_STATE_CRASHED;
            return -1;
        }
        ret = 0;
    }

    if (ret < 0) {
        vm->state = VM_STATE_CRASHED;
        return -1;
    }

    /* crosvm VMs stay BOOTING until the launcher daemon reports a live pid;
       container-backed VMs are genuinely running now. */
    if (vm->type != VM_TYPE_CROSVM)
        vm->state = VM_STATE_RUNNING;
    kprintf("vm: started VM '%s' (id=%d, state=%s)\n",
            vm->name, vm->id, vm_state_str(vm->state));
    return 0;
}

int vm_stop(int vm_id) {
    vm_sync_states();
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;

    if (vm->state != VM_STATE_RUNNING && vm->state != VM_STATE_PAUSED) {
        kprintf("vm: VM '%s' is not running\n", vm->name);
        return -1;
    }

    vm->state = VM_STATE_STOPPING;

    int ret = -1;
    if (vm->type == VM_TYPE_CROSVM) {
        ret = crosvm_stop_vm(vm);
    } else {
        container_stop(vm->container_id);
        ret = 0;
    }

    if (ret < 0) {
        vm->state = VM_STATE_RUNNING;
        return -1;
    }

    vm->state = VM_STATE_STOPPED;
    vm->stop_time = timer_get_milliseconds();

    kprintf("vm: stopped VM '%s' (uptime=%lu ms)\n",
            vm->name, vm->stop_time - vm->start_time);
    return 0;
}

int vm_destroy(int vm_id) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) {
        for (int i = 0; i < VM_MAX; i++)
            if (vms[i].id == vm_id) { vm = &vms[i]; break; }
    }
    if (!vm) return -1;

    if (vm->state == VM_STATE_RUNNING || vm->state == VM_STATE_PAUSED)
        vm_stop(vm_id);

    /* Destroy the underlying resource */
    if (vm->type == VM_TYPE_CROSVM) {
        /* Remove any leftover launcher IPC files */
        char fb[256];
        snprintf(fb, sizeof(fb), "%s/%s.cmd", CROSVM_CMD_DIR, vm->name);
        fs_rm(fb);
        snprintf(fb, sizeof(fb), "%s/%s.ctl", CROSVM_CMD_DIR, vm->name);
        fs_rm(fb);
        snprintf(fb, sizeof(fb), "%s/%s.pid", CROSVM_CMD_DIR, vm->name);
        fs_rm(fb);
        snprintf(fb, sizeof(fb), "%s/%s.exit", CROSVM_CMD_DIR, vm->name);
        fs_rm(fb);
        vm->container_id = 0;
    } else {
        /* Destroy the underlying container (releases namespaces + cgroups) */
        container_destroy(vm->container_id);
    }

    int saved = vm->id;
    memset(vm, 0, sizeof(vm_t));
    kprintf("vm: destroyed VM id=%d\n", saved);
    return 0;
}

int vm_pause(int vm_id) {
    vm_sync_states();
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    if (vm->state != VM_STATE_RUNNING) return -1;

    int ret = -1;
    if (vm->type == VM_TYPE_CROSVM) {
        ret = crosvm_pause_vm(vm);
    } else {
        vm->state = VM_STATE_PAUSED;
        container_set_state(vm->container_id, CONTAINER_PAUSED);
        ret = 0;
    }
    
    if (ret == 0) {
        kprintf("vm: paused VM '%s'\n", vm->name);
    }
    return ret;
}

int vm_resume(int vm_id) {
    vm_sync_states();
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    if (vm->state != VM_STATE_PAUSED) return -1;

    int ret = -1;
    if (vm->type == VM_TYPE_CROSVM) {
        ret = crosvm_resume_vm(vm);
    } else {
        vm->state = VM_STATE_RUNNING;
        container_set_state(vm->container_id, CONTAINER_RUNNING);
        ret = 0;
    }
    
    if (ret == 0) {
        kprintf("vm: resumed VM '%s'\n", vm->name);
    }
    return ret;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VM queries
 * ══════════════════════════════════════════════════════════════════════════════ */

vm_t *vm_get(int vm_id) {
    for (int i = 0; i < VM_MAX; i++)
        if (vms[i].id == vm_id) return &vms[i];
    return 0;
}

vm_t *vm_find(const char *name) {
    for (int i = 0; i < VM_MAX; i++)
        if (vms[i].id > 0 && strcmp(vms[i].name, name) == 0)
            return &vms[i];
    return 0;
}

int vm_list(char names[][VM_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < VM_MAX && count < max; i++) {
        if (vms[i].state == VM_STATE_RUNNING) {
            strlcpy(names[count], vms[i].name, VM_NAME_MAX);
            count++;
        }
    }
    return count;
}

int vm_list_all(char names[][VM_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < VM_MAX && count < max; i++) {
        if (vms[i].id > 0) {
            strlcpy(names[count], vms[i].name, VM_NAME_MAX);
            count++;
        }
    }
    return count;
}

int vm_get_state(int vm_id) {
    vm_t *vm = vm_get(vm_id);
    return vm ? (int)vm->state : -1;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VM exec
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_exec(int vm_id, const char *path, int argc, char **argv, char **envp) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    if (vm->state != VM_STATE_RUNNING) return -1;
    return container_exec(vm->container_id, path, argc, argv, envp);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VM info / inspect
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_inspect(int vm_id, char *buf, int max) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;

    int n = 0;
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "VM: %s\n", vm->name);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  ID:       %d\n", vm->id);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  State:    %s\n", vm_state_str(vm->state));
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Type:     %s\n",
                  vm_type_str(vm->type));
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Container: %d\n", vm->container_id);

    if (vm->state == VM_STATE_RUNNING) {
        uint64_t elapsed = timer_get_milliseconds() - vm->start_time;
        n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Uptime:   %lu ms\n", elapsed);
    }

    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Display:  %dx%d@%d\n",
                  vm->display_width, vm->display_height, vm->display_bpp);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Memory:   %d MB configured\n",
                  vm->config.memory_mb);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  CPUs:     %d\n",
                  vm->config.cpu_count);
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  Network:  %s\n",
                  vm->config.enable_network ? "enabled" : "disabled");
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  9p:       %s\n",
                  vm->config.enable_9p ? "enabled" : "disabled");
    n += snprintf(buf + n, (size_t)(n < max ? max - n : 0), "  GPU:      %s\n",
                  vm->config.enable_gpu ? "enabled" : "disabled");

    return n;
}

int vm_logs(int vm_id, char *buf, int max) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    return container_logs(vm->container_id, buf, max);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VM resource limits
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_set_memory_limit(int vm_id, uint64_t mb) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    vm->config.memory_mb = mb;
    container_t *c = container_get(vm->container_id);
    if (c) {
        cgroup_set_memory_limit(c->cgroup_id, mb * 1024 * 1024);
    }
    return 0;
}

int vm_set_cpu_count(int vm_id, int count) {
    vm_t *vm = vm_get(vm_id);
    if (!vm) return -1;
    vm->config.cpu_count = count;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Pre-configured VM templates
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_create_linux(const char *name, const char *rootfs, uint64_t memory_mb) {
    vm_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strlcpy(cfg.name, name, VM_NAME_MAX);
    cfg.type = VM_TYPE_LINUX_CONTAINER;
    if (rootfs) strlcpy(cfg.rootfs_path, rootfs, VM_IMAGE_PATH_MAX);
    else strlcpy(cfg.rootfs_path, LINUX_ROOTFS_DEFAULT, VM_IMAGE_PATH_MAX);
    cfg.memory_mb = memory_mb ? memory_mb : 256;
    cfg.cpu_count = 1;
    cfg.enable_network = 1;
    cfg.enable_9p = 1;
    strlcpy(cfg.shared_path, "/home", VM_IMAGE_PATH_MAX);
    strlcpy(cfg.shared_mount, "/mnt/host", VM_IMAGE_PATH_MAX);
    return vm_create(&cfg);
}

int vm_create_android(const char *name, const char *rootfs, uint64_t memory_mb) {
    vm_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strlcpy(cfg.name, name, VM_NAME_MAX);
    cfg.type = VM_TYPE_ANDROID_CONTAINER;
    if (rootfs) strlcpy(cfg.rootfs_path, rootfs, VM_IMAGE_PATH_MAX);
    else strlcpy(cfg.rootfs_path, ANDROID_ROOTFS_DEFAULT, VM_IMAGE_PATH_MAX);
    cfg.memory_mb = memory_mb ? memory_mb : 512;
    cfg.cpu_count = 2;
    cfg.enable_network = 1;
    cfg.enable_gpu = 1;
    cfg.enable_9p = 1;
    strlcpy(cfg.shared_path, "/home", VM_IMAGE_PATH_MAX);
    strlcpy(cfg.shared_mount, "/mnt/host", VM_IMAGE_PATH_MAX);
    return vm_create(&cfg);
}

int vm_create_crosvm(const char *name, const char *kernel, const char *initrd,
                     const char *rootfs, uint64_t memory_mb, int cpu_count) {
    vm_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strlcpy(cfg.name, name, VM_NAME_MAX);
    cfg.type = VM_TYPE_CROSVM;
    if (kernel) strlcpy(cfg.kernel_path, kernel, VM_IMAGE_PATH_MAX);
    if (initrd) strlcpy(cfg.init_path, initrd, VM_IMAGE_PATH_MAX);
    if (rootfs) strlcpy(cfg.rootfs_path, rootfs, VM_IMAGE_PATH_MAX);
    cfg.memory_mb = memory_mb ? memory_mb : 1024;
    cfg.cpu_count = cpu_count ? cpu_count : 2;
    cfg.enable_network = 1;
    cfg.enable_gpu = 0;  /* crosvm handles GPU via virtio */
    cfg.enable_9p = 1;
    strlcpy(cfg.shared_path, "/home", VM_IMAGE_PATH_MAX);
    strlcpy(cfg.shared_mount, "/mnt/host", VM_IMAGE_PATH_MAX);
    return vm_create(&cfg);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Concierge-style API
 * ══════════════════════════════════════════════════════════════════════════════ */

int vm_concierge_list(void) {
    char names[VM_MAX][VM_NAME_MAX];
    int count = vm_list_all(names, VM_MAX);
    kprintf("vm: concierge listing %d VMs\n", count);
    for (int i = 0; i < count; i++) {
        vm_t *vm = vm_find(names[i]);
        if (vm) {
            kprintf("  [%d] %s - %s (%s)\n", vm->id, vm->name,
                    vm_state_str(vm->state), vm_type_str(vm->type));
        }
    }
    return count;
}

int vm_concierge_create(const char *name, const char *image, uint64_t memory_mb) {
    return vm_create_linux(name, image, memory_mb);
}

int vm_concierge_create_crosvm(const char *name, const char *kernel, const char *initrd,
                               const char *rootfs, uint64_t memory_mb, int cpu_count) {
    return vm_create_crosvm(name, kernel, initrd, rootfs, memory_mb, cpu_count);
}

int vm_concierge_start(const char *name) {
    vm_t *vm = vm_find(name);
    if (!vm) return -1;
    return vm_start(vm->id);
}

int vm_concierge_stop(const char *name) {
    vm_t *vm = vm_find(name);
    if (!vm) return -1;
    return vm_stop(vm->id);
}

int vm_concierge_destroy(const char *name) {
    vm_t *vm = vm_find(name);
    if (!vm) return -1;
    return vm_destroy(vm->id);
}

int vm_concierge_snapshot(const char *name, const char *snapshot_name) {
    vm_t *vm = vm_find(name);
    if (!vm) return -1;
    if (vm->type != VM_TYPE_CROSVM) return -1;
    return crosvm_snapshot_vm(vm, snapshot_name);
}
