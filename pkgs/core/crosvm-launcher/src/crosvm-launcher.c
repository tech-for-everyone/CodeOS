/* crosvm-launcher — userspace launcher for crosvm VMs.
 *
 * The kernel VM manager writes a command file to /tmp/crosvm-cmds/<name>.cmd
 * containing the full crosvm command line. This daemon polls that directory,
 * forks a per-VM supervisor (so multiple VMs run concurrently and control
 * requests are never starved), tracks PIDs, and answers lifecycle requests
 * (stop/pause/resume/snapshot) via .ctl files.
 *
 * Feedback files written back for the kernel:
 *   <name>.pid   — PID of the running crosvm process
 *   <name>.exit  — "<status>" written by the supervisor when the VM exits
 *
 * Uses only CodeOS kernel syscalls (no POSIX shm/signals).
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define CMD_DIR      "/tmp/crosvm-cmds"
#define MAX_CMD_LEN  4096
#define MAX_ARGS     128
#define MAX_POLL     4096
#define POLL_MS      200

#define CROSVM_BINARY     "/usr/bin/crosvm"
#define CROSVM_SOCKET_DIR "/run/crosvm"

/* Static VM tracking table */
#define MAX_VMS 8
typedef struct {
    char name[32];
    int  pid;
    int  running;
} vm_entry_t;

static vm_entry_t vms[MAX_VMS];

static int find_vm(const char *name) {
    for (int i = 0; i < MAX_VMS; i++)
        if (vms[i].name[0] && strcmp(vms[i].name, name) == 0)
            return i;
    return -1;
}

static int alloc_vm(void) {
    for (int i = 0; i < MAX_VMS; i++)
        if (!vms[i].name[0]) return i;
    return -1;
}

static void forget_vm(int idx) {
    if (idx < 0 || idx >= MAX_VMS) return;
    vms[idx].name[0] = 0;
    vms[idx].pid = 0;
    vms[idx].running = 0;
}

/* Only safe names reach the filesystem: [A-Za-z0-9._-]+, no leading '.' */
static int valid_name(const char *name) {
    if (!name || !name[0] || name[0] == '.') return 0;
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    }
    return 1;
}

/* Split a command line into argv[]. Returns argc. argv is NUL-terminated. */
static int tokenize(char *cmd, char *argv[], int max) {
    int argc = 0;
    char *p = cmd;
    while (*p && argc < max - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
    }
    argv[argc] = 0;
    return argc;
}

/* Read a whole file into buf. Returns byte count or <0. */
static int read_file(const char *path, char *buf, int max) {
    int fd = sys_open(path, 0);
    if (fd < 0) return -1;
    int total = 0;
    int n;
    while ((n = sys_read(fd, buf + total, max - total)) > 0)
        total += n;
    sys_close(fd);
    buf[total] = 0;
    return total;
}

static int write_file(const char *path, const char *data) {
    int fd = sys_open(path, 1);
    if (fd < 0) fd = sys_open(path, 2);
    if (fd < 0) return -1;
    int n = sys_write(data, strlen(data));
    sys_close(fd);
    return n;
}

/* Supervisor child: waits for the VM process and records the exit status so
 * both the daemon and the kernel can observe VM termination. Runs in its own
 * process, so the daemon's poll loop is never blocked by a running VM. */
static void run_supervisor(const char *name, char *cmd) {
    char *argv[MAX_ARGS];
    int argc = tokenize(cmd, argv, MAX_ARGS);
    if (argc <= 0) sys_exit(1);

    int pid = sys_fork();
    if (pid < 0) sys_exit(1);
    if (pid == 0) {
        /* Grandchild: become the VM */
        sys_set_personality(PERSONALITY_LINUX);
        sys_execve(argv[0], argv, argc);
        printf("crosvm-launcher: exec '%s' failed\n", argv[0]);
        sys_exit(127);
    }

    /* Child = supervisor: publish the VM pid, wait, publish exit status */
    {
        char pidfile[128];
        char pbuf[16];
        snprintf(pidfile, sizeof(pidfile), "%s/%s.pid", CMD_DIR, name);
        snprintf(pbuf, sizeof(pbuf), "%d\n", pid);
        write_file(pidfile, pbuf);
    }
    printf("crosvm-launcher: '%s' running as pid %d\n", name, pid);

    int status = 0;
    sys_wait(pid, &status);

    {
        char exitfile[128];
        char sbuf[32];
        snprintf(exitfile, sizeof(exitfile), "%s/%s.exit", CMD_DIR, name);
        snprintf(sbuf, sizeof(sbuf), "%d\n", status);
        write_file(exitfile, sbuf);
    }

    /* Daemon-owned marker: tells the poll loop this entry is finished */
    {
        char supfile[128];
        snprintf(supfile, sizeof(supfile), "%s/%s.sup", CMD_DIR, name);
        write_file(supfile, "1\n");
    }

    printf("crosvm-launcher: '%s' exited (status %d)\n", name, status);
    sys_exit(0);
}

/* Launch the VM described by <name>.cmd. Non-blocking: a supervisor process
 * tracks the VM while the daemon keeps servicing polls and ctl requests. */
static int launch_vm(const char *name) {
    char cmd_path[128];
    snprintf(cmd_path, sizeof(cmd_path), "%s/%s.cmd", CMD_DIR, name);

    char cmd[MAX_CMD_LEN];
    int len = read_file(cmd_path, cmd, sizeof(cmd) - 1);
    if (len <= 0) {
        printf("crosvm-launcher: no command for '%s'\n", name);
        return -1;
    }

    if (!valid_name(name)) {
        printf("crosvm-launcher: rejecting unsafe VM name '%s'\n", name);
        sys_unlink(cmd_path);
        return -1;
    }

    int idx = find_vm(name);
    if (idx < 0) idx = alloc_vm();
    if (idx < 0) {
        printf("crosvm-launcher: too many VMs\n");
        return -1;
    }
    if (vms[idx].running) {
        printf("crosvm-launcher: '%s' already running (pid %d)\n", name, vms[idx].pid);
        sys_unlink(cmd_path);
        return -1;
    }

    printf("crosvm-launcher: launching '%s': %s\n", name, cmd);

    int sup = sys_fork();
    if (sup < 0) {
        printf("crosvm-launcher: fork failed\n");
        return -1;
    }
    if (sup == 0)
        run_supervisor(name, cmd);   /* never returns */

    /* Daemon: track the supervisor's VM without waiting for it */
    strncpy(vms[idx].name, name, 31);
    vms[idx].name[31] = 0;
    vms[idx].pid = sup;              /* supervisor pid stands in until .pid lands */
    vms[idx].running = 1;
    printf("crosvm-launcher: '%s' launching (supervisor pid %d)\n", name, sup);

    /* Remove the .cmd so we don't relaunch it on the next poll */
    sys_unlink(cmd_path);
    return 0;
}

/* Reconcile tracked VMs: a <name>.sup marker means the supervisor finished
 * and published .exit for the kernel. The daemon owns .sup; the kernel owns
 * .pid and .exit. */
static void reconcile_vms(void) {
    for (int i = 0; i < MAX_VMS; i++) {
        if (!vms[i].name[0] || !vms[i].running) continue;

        char path[128];
        char buf[32];

        snprintf(path, sizeof(path), "%s/%s.sup", CMD_DIR, vms[i].name);
        if (read_file(path, buf, sizeof(buf) - 1) > 0) {
            sys_unlink(path);
            printf("crosvm-launcher: '%s' finished\n", vms[i].name);
            forget_vm(i);
        }
    }
}

/* Lifecycle request from kernel: <name>.ctl contains "stop"|"pause"|"resume"
 * or "snapshot <name>". Signals handle process lifecycle; snapshots drive
 * crosvm's control socket directly. */
static void handle_ctl(const char *name) {
    char ctl_path[128];
    snprintf(ctl_path, sizeof(ctl_path), "%s/%s.ctl", CMD_DIR, name);
    char ctl[64];
    int n = read_file(ctl_path, ctl, sizeof(ctl) - 1);
    if (n <= 0) return;
    ctl[n] = 0;
    if (ctl[n - 1] == '\n') ctl[n - 1] = 0;

    if (!valid_name(name)) {
        sys_unlink(ctl_path);
        return;
    }

    int idx = find_vm(name);
    if ((idx < 0 || !vms[idx].running || vms[idx].pid <= 0)
        && strncmp(ctl, "snapshot", 8) != 0) {
        printf("crosvm-launcher: '%s' not running, ignoring %s\n", name, ctl);
        sys_unlink(ctl_path);
        return;
    }

    printf("crosvm-launcher: '%s' <- %s\n", name, ctl);

    if (strcmp(ctl, "stop") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGTERM);
        vms[idx].running = 0;          /* supervisor publishes the final status */
    } else if (strcmp(ctl, "pause") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGSTOP);
    } else if (strcmp(ctl, "resume") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGCONT);
    } else if (strncmp(ctl, "snapshot", 8) == 0) {
        const char *snap = ctl[8] == ' ' ? ctl + 9 : "";
        if (!valid_name(snap)) {
            printf("crosvm-launcher: invalid snapshot name\n");
        } else {
            /* crosvm snapshot <control-socket> <directory> */
            char sock[128];
            char dir[160];
            snprintf(sock, sizeof(sock), "%s/%s.sock", CROSVM_SOCKET_DIR, name);
            snprintf(dir, sizeof(dir), "/var/lib/crosvm/%s/snapshots/%s", name, snap);
            int pid = sys_fork();
            if (pid == 0) {
                sys_set_personality(PERSONALITY_LINUX);
                char *argv[] = { (char *)CROSVM_BINARY, "snapshot", sock, dir, 0 };
                sys_execve(argv[0], argv, 4);
                sys_exit(127);
            }
            printf("crosvm-launcher: snapshot '%s' of '%s' requested (%s)\n",
                   snap, name, dir);
        }
    }

    sys_unlink(ctl_path);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    memset(vms, 0, sizeof(vms));

    printf("crosvm-launcher: starting (pid %d)\n", sys_getpid());
    printf("crosvm-launcher: polling %s for VM commands\n", CMD_DIR);

    for (;;) {
        /* Poll the command directory */
        char names[MAX_POLL];
        int n = sys_readdir(CMD_DIR, names, sizeof(names));
        if (n > 0) {
            /* sys_readdir returns NUL-separated entries; walk them */
            int off = 0;
            while (off < n) {
                const char *entry = names + off;
                int elen = 0;
                while (off < n && names[off]) { off++; elen++; }
                off++; /* skip NUL */
                if (elen <= 0) continue;

                int el = elen;
                if (el > 4 && strcmp(entry + el - 4, ".cmd") == 0) {
                    char vname[64];
                    int vlen = el - 4;
                    if (vlen > 63) vlen = 63;
                    memcpy(vname, entry, vlen);
                    vname[vlen] = 0;
                    launch_vm(vname);
                } else if (el > 4 && strcmp(entry + el - 4, ".ctl") == 0) {
                    char vname[64];
                    int vlen = el - 4;
                    if (vlen > 63) vlen = 63;
                    memcpy(vname, entry, vlen);
                    vname[vlen] = 0;
                    handle_ctl(vname);
                }
            }
        }
        reconcile_vms();
        sys_sleep(POLL_MS);
    }
    return 0;
}
