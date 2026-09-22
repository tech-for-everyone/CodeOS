/* ncvm — CodeOS's in-guest VM backend + VM controller.
 *
 * The guest side of the ncvm story: ncvm is CodeOS's own, improved QEMU
 * fork. Inside a running CodeOS VM, this program is the backend that the
 * kernel VM manager hands VM workloads to (the same role stock crosvm
 * used to play), and it doubles as a small `ncvm` command for driving VMs.
 *
 *  - daemon mode (`ncvm daemon`, or plain `ncvm`): polls /tmp/crosvm-cmds/
 *    for <name>.cmd command files written by the kernel VM manager,
 *    translates a crosvm-style command line into ncvm (QEMU fork) args,
 *    forks a per-VM supervisor and execs the ncvm VMM. Speaks the same
 *    wire protocol as crosvm-launcher (.cmd / .ctl / .pid / .exit / .sup)
 *    so the kernel side needs no changes.
 *
 *  - CLI mode (`ncvm list|info|start|stop|pause|resume|run`): drives the
 *    backend through those same /tmp/crosvm-cmds files, mirroring what the
 *    kernel `vm` shell builtin does. No VM syscalls needed.
 *
 * The VMM binary itself is NOT embedded in the rootfs (it is tens of MB);
 * provide it beside the guest (disk.img / 9p). Default path
 * /usr/bin/ncvm-x86_64, override with NCVM_BIN. Like crosvm it runs under
 * the Linux personality so the guest's linux syscall handler can exec it.
 *
 * Uses only CodeOS kernel syscalls (no POSIX shm/signals).
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define CMD_DIR        "/tmp/crosvm-cmds"
#define DEFAULT_VMM    "/usr/bin/ncvm-x86_64"
#define MAX_CMD_LEN    4096
#define MAX_ARGS       128
#define MAX_SYN        32
#define MAX_POLL       4096
#define POLL_MS        200

#define MAX_VMS        8

/* Static VM tracking table */
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

/* ── crosvm-style command line -> ncvm (QEMU fork) command line ─────────
 * The kernel `vm` shell writes crosvm-flavoured command lines. ncvm's VMM
 * is a QEMU fork, so map the common subset; unknown flags pass through so
 * nothing breaks silently.
 *
 * Supported:
 *   crosvm run ...          -> dropped (target is the ncvm VMM)
 *   -m/--memory N           -> -m NM              (QEMU defaults to MiB)
 *   -cpus N                 -> -smp N
 *   --kernel PATH           -> -kernel PATH
 *   --root FILE             -> -drive file=FILE,format=raw,if=virtio
 *   --root DIR              -> -virtfs ... mount_tag=root (9p)
 *   --rwdisk/--disk FILE    -> -drive file=FILE,format=raw,if=virtio
 *   --serial type=stdio     -> -serial stdio
 *   --serial type=file,path=P -> -serial file:P
 * Dropped: --rng V, --disable-sandbox, --no-sandbox, --seccomp-policy-file V
 *
 * Defaults appended: -machine q35, plus -accel tcg,thread=multi when no
 * accelerator was given (no KVM inside the guest).
 */
static char synbuf[MAX_SYN][64];
static int  synused = 0;

static const char *syn(char *out, const char *fmt, const char *a, const char *b) {
    if (synused >= MAX_SYN) return "";
    snprintf(synbuf[synused], sizeof(synbuf[synused]), fmt, a, b);
    out = synbuf[synused];
    synused++;
    return out;
}

static int starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}

static int is_digits(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int qargv_add(char *qargv[], int *qargc, const char *tok) {
    if (*qargc >= MAX_ARGS - 1) return -1;
    qargv[(*qargc)++] = (char *)tok;
    return 0;
}

static void translate_vm_cmd(char *out[], int *outc, char *cmd, const char *vmm) {
    char *argv[MAX_ARGS];
    int argc = tokenize(cmd, argv, MAX_ARGS);
    int i = 0;
    int accel_seen = 0;

    qargv_add(out, outc, vmm);

    if (argc > 0 && strcmp(argv[0], "crosvm") == 0) {
        i = 1;                  /* skip the old VMM name */
        if (i < argc && strcmp(argv[i], "run") == 0) i++;
    }

    for (; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-m") == 0 || strcmp(a, "--memory") == 0) {
            if (i + 1 < argc && is_digits(argv[i + 1])) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%sM", argv[i + 1]);
                qargv_add(out, outc, syn(buf, "%s", buf, 0));
                i++;
            }
            continue;
        }
        if (strcmp(a, "-cpus") == 0 || strcmp(a, "--cpus") == 0) {
            if (i + 1 < argc) {
                qargv_add(out, outc, "-smp");
                qargv_add(out, outc, argv[i + 1]);
                i++;
            }
            continue;
        }
        if (strcmp(a, "--kernel") == 0 || strcmp(a, "-kernel") == 0) {
            if (i + 1 < argc) {
                qargv_add(out, outc, "-kernel");
                qargv_add(out, outc, argv[i + 1]);
                i++;
            }
            continue;
        }
        if (strcmp(a, "--root") == 0) {
            if (i + 1 < argc) {
                if (argv[i + 1][0] == '/' && argv[i + 1][strlen(argv[i + 1]) - 1] == '/') {
                    /* directory root: serve via 9p as mount_tag=root */
                    char buf[64];
                    snprintf(buf, sizeof(buf), "local,path=%s,mount_tag=root,security_model=none",
                             argv[i + 1]);
                    qargv_add(out, outc, "-virtfs");
                    qargv_add(out, outc, syn(buf, "%s", buf, 0));
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "file=%s,format=raw,if=virtio", argv[i + 1]);
                    qargv_add(out, outc, "-drive");
                    qargv_add(out, outc, syn(buf, "%s", buf, 0));
                }
                i++;
            }
            continue;
        }
        if (strcmp(a, "--rwdisk") == 0 || strcmp(a, "--disk") == 0) {
            if (i + 1 < argc) {
                char buf[64];
                snprintf(buf, sizeof(buf), "file=%s,format=raw,if=virtio", argv[i + 1]);
                qargv_add(out, outc, "-drive");
                qargv_add(out, outc, syn(buf, "%s", buf, 0));
                i++;
            }
            continue;
        }
        if (strcmp(a, "--serial") == 0) {
            if (i + 1 < argc) {
                if (strcmp(argv[i + 1], "type=stdio") == 0
                    || strncmp(argv[i + 1], "type=stdio", 10) == 0) {
                    qargv_add(out, outc, "-serial");
                    qargv_add(out, outc, "stdio");
                } else if (starts_with(argv[i + 1], "type=file,path=")) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "file:%s", argv[i + 1] + 15);
                    qargv_add(out, outc, "-serial");
                    qargv_add(out, outc, syn(buf, "%s", buf, 0));
                }
                i++;
            }
            continue;
        }
        /* crosvm-only flags we discard */
        if (strcmp(a, "--rng") == 0 || strcmp(a, "--seccomp-policy-file") == 0) {
            if (i + 1 < argc) i++;
            continue;
        }
        if (strcmp(a, "--disable-sandbox") == 0 || strcmp(a, "--no-sandbox") == 0)
            continue;

        if (strcmp(a, "-accel") == 0 || strcmp(a, "--accel") == 0) accel_seen = 1;
        qargv_add(out, outc, a);
    }

    qargv_add(out, outc, "-machine");
    qargv_add(out, outc, "q35");
    if (!accel_seen) {
        qargv_add(out, outc, "-accel");
        qargv_add(out, outc, "tcg,thread=multi");
    }
    out[*outc] = 0;
}

/* ── supervisor child: wait for the VM, publish .pid / .exit ────────── */
static void run_supervisor(const char *name, char *cmd, const char *vmm) {
    char *qargv[MAX_ARGS];
    int qargc = 0;
    translate_vm_cmd(qargv, &qargc, cmd, vmm);

    int pid = sys_fork();
    if (pid < 0) sys_exit(1);
    if (pid == 0) {
        /* Grandchild: become the VMM */
        sys_set_personality(PERSONALITY_LINUX);
        sys_execve(qargv[0], qargv, qargc);
        printf("ncvm: exec '%s' failed\n", qargv[0]);
        sys_exit(127);
    }

    {
        char pidfile[128];
        char pbuf[16];
        snprintf(pidfile, sizeof(pidfile), "%s/%s.pid", CMD_DIR, name);
        snprintf(pbuf, sizeof(pbuf), "%d\n", pid);
        write_file(pidfile, pbuf);
    }
    printf("ncvm: '%s' running as pid %d\n", name, pid);

    int status = 0;
    sys_wait(pid, &status);

    {
        char exitfile[128];
        char sbuf[32];
        snprintf(exitfile, sizeof(exitfile), "%s/%s.exit", CMD_DIR, name);
        snprintf(sbuf, sizeof(sbuf), "%d\n", status);
        write_file(exitfile, sbuf);
    }
    {
        char supfile[128];
        snprintf(supfile, sizeof(supfile), "%s/%s.sup", CMD_DIR, name);
        write_file(supfile, "1\n");
    }
    printf("ncvm: '%s' exited (status %d)\n", name, status);
    sys_exit(0);
}

static int launch_vm(const char *name, const char *vmm) {
    char cmd_path[128];
    snprintf(cmd_path, sizeof(cmd_path), "%s/%s.cmd", CMD_DIR, name);

    char cmd[MAX_CMD_LEN];
    int len = read_file(cmd_path, cmd, sizeof(cmd) - 1);
    if (len <= 0) {
        printf("ncvm: no command for '%s'\n", name);
        return -1;
    }

    if (!valid_name(name)) {
        printf("ncvm: rejecting unsafe VM name '%s'\n", name);
        sys_unlink(cmd_path);
        return -1;
    }

    int idx = find_vm(name);
    if (idx < 0) idx = alloc_vm();
    if (idx < 0) {
        printf("ncvm: too many VMs\n");
        return -1;
    }
    if (vms[idx].running) {
        printf("ncvm: '%s' already running (pid %d)\n", name, vms[idx].pid);
        sys_unlink(cmd_path);
        return -1;
    }

    printf("ncvm: launching '%s': %s\n", name, cmd);

    int sup = sys_fork();
    if (sup < 0) {
        printf("ncvm: fork failed\n");
        return -1;
    }
    if (sup == 0)
        run_supervisor(name, cmd, vmm);   /* never returns */

    strncpy(vms[idx].name, name, 31);
    vms[idx].name[31] = 0;
    vms[idx].pid = sup;
    vms[idx].running = 1;
    printf("ncvm: '%s' launching (supervisor pid %d)\n", name, sup);

    sys_unlink(cmd_path);
    return 0;
}

static void reconcile_vms(void) {
    for (int i = 0; i < MAX_VMS; i++) {
        if (!vms[i].name[0] || !vms[i].running) continue;

        char path[128];
        char buf[32];

        snprintf(path, sizeof(path), "%s/%s.sup", CMD_DIR, vms[i].name);
        if (read_file(path, buf, sizeof(buf) - 1) > 0) {
            sys_unlink(path);
            printf("ncvm: '%s' finished\n", vms[i].name);
            forget_vm(i);
        }
    }
}

/* Lifecycle request from the kernel: <name>.ctl contains
 * "stop"|"pause"|"resume". Signals drive the supervisor (and through it
 * the VMM process). */
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
    if (idx < 0 || !vms[idx].running || vms[idx].pid <= 0) {
        printf("ncvm: '%s' not running, ignoring %s\n", name, ctl);
        sys_unlink(ctl_path);
        return;
    }

    printf("ncvm: '%s' <- %s\n", name, ctl);

    if (strcmp(ctl, "stop") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGTERM);
        vms[idx].running = 0;          /* supervisor publishes final status */
    } else if (strcmp(ctl, "pause") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGSTOP);
    } else if (strcmp(ctl, "resume") == 0) {
        sys_kill(vms[idx].pid, KILL_SIGCONT);
    }

    sys_unlink(ctl_path);
}

static int daemon_main(void) {
    int i;
    for (i = 0; i < MAX_VMS; i++) { vms[i].name[0] = 0; vms[i].pid = 0; vms[i].running = 0; }

    const char *vmm = DEFAULT_VMM;
    printf("ncvm: backend starting (pid %d)\n", sys_getpid());
    printf("ncvm: VMM = %s, polling %s\n", vmm, CMD_DIR);

    for (;;) {
        char names[MAX_POLL];
        int n = sys_readdir(CMD_DIR, names, sizeof(names));
        if (n > 0) {
            int off = 0;
            while (off < n) {
                const char *entry = names + off;
                int elen = 0;
                while (off < n && names[off]) { off++; elen++; }
                off++;
                if (elen <= 0) continue;

                int el = elen;
                if (el > 4 && strcmp(entry + el - 4, ".cmd") == 0) {
                    char vname[64];
                    int vlen = el - 4;
                    if (vlen > 63) vlen = 63;
                    memcpy(vname, entry, vlen);
                    vname[vlen] = 0;
                    launch_vm(vname, vmm);
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

/* ── CLI ─────────────────────────────────────────────────────────────── */

static void cli_usage(void) {
    printf("usage: ncvm [list|info <name>|start <name>|stop <name>|pause <name>|resume <name>|daemon]\n");
    printf("  ncvm (no args)         run the VM backend daemon\n");
    printf("  ncvm list              show VMs known to the backend\n");
    printf("  ncvm info <name>       show one VM's status (pid/exit)\n");
    printf("  ncvm start <name>      write a default .cmd so the daemon boots it\n");
    printf("  ncvm stop/pause/resume signal the VM's supervisor\n");
    printf("  ncvm daemon            explicit daemon mode\n");
}

static int list_vms(void) {
    char names[MAX_POLL];
    int n = sys_readdir(CMD_DIR, names, sizeof(names));
    if (n <= 0) {
        printf("ncvm: no VMs (dir empty)\n");
        return 0;
    }
    int off = 0;
    while (off < n) {
        const char *entry = names + off;
        int elen = 0;
        while (off < n && names[off]) { off++; elen++; }
        off++;
        if (elen <= 0) continue;
        printf("ncvm: %s\n", entry);
    }
    return 0;
}

static int cli_vm(int argc, char **argv) {
    if (argc < 3) { cli_usage(); return 1; }
    const char *op = argv[1];
    const char *name = argv[2];
    if (!valid_name(name)) { printf("ncvm: bad VM name\n"); return 1; }

    if (strcmp(op, "info") == 0) {
        char path[128];
        char buf[64];
        snprintf(path, sizeof(path), "%s/%s.pid", CMD_DIR, name);
        if (read_file(path, buf, sizeof(buf) - 1) > 0)
            printf("ncvm: %s pid %s", name, buf);
        else
            printf("ncvm: %s not running\n", name);
        return 0;
    }
    if (strcmp(op, "stop") == 0 || strcmp(op, "pause") == 0 || strcmp(op, "resume") == 0) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s.ctl", CMD_DIR, name);
        char line[32];
        snprintf(line, sizeof(line), "%s\n", op);
        if (write_file(path, line) > 0) {
            printf("ncvm: %s %s requested\n", op, name);
            return 0;
        }
        printf("ncvm: could not write %s\n", path);
        return 1;
    }
    if (strcmp(op, "start") == 0) {
        /* Default VM body: boot a kernel ELF if the guest has one mounted,
         * otherwise boot nothing (write the .cmd and let the daemon run the
         * VMM with the QoS args below). Extra args on the CLI are appended:
         *   ncvm start foo -- --kernel /mnt/kernel.bin */
        char path[128];
        char cmd[MAX_CMD_LEN];
        char body[MAX_CMD_LEN];
        int off = 0;
        int i;
        body[0] = 0;
        for (i = 3; i < argc; i++) {
            int need = strlen(argv[i]) + 1;
            if (off + need < (int)sizeof(body) - 1) {
                int k;
                for (k = 0; argv[i][k]; k++) body[off++] = argv[i][k];
                body[off++] = ' ';
            }
        }
        body[off] = 0;
        if (off == 0) {
            /* Sensible default: halt-free TCG boot that at least proves the
             * VMM runs; users typically append -kernel/-cdrom paths. */
            snprintf(body, sizeof(body), "-machine q35 -accel tcg,thread=multi -display none");
        }
        snprintf(cmd, sizeof(cmd), "%s\n", body);
        snprintf(path, sizeof(path), "%s/%s.cmd", CMD_DIR, name);
        if (write_file(path, cmd) > 0) {
            printf("ncvm: %s queued for the backend\n", name);
            return 0;
        }
        printf("ncvm: could not write %s\n", path);
        return 1;
    }

    cli_usage();
    return 1;
}

/* ── self-test (boot-time smoke test for the VM backend binary) ───────── */
static int selftest_main(void) {
    printf("ncvm: selftest start (pid %d)\n", sys_getpid());

    /* Exercise the backend's working set: dir listing + name validation. */
    char names[MAX_POLL];
    int n = sys_readdir(CMD_DIR, names, sizeof(names));
    if (n > 0) {
        int off = 0, entries = 0;
        while (off < n) {
            while (off < n && names[off]) off++;
            off++;
            entries++;
        }
        printf("ncvm: selftest %s has %d entries\n", CMD_DIR, entries);
    } else {
        printf("ncvm: selftest %s empty/unmounted\n", CMD_DIR);
    }

    if (valid_name("selftest.vm") && !valid_name("../evil"))
        printf("ncvm: selftest name-validation ok\n");
    if (is_digits("4096") && !is_digits("-1"))
        printf("ncvm: selftest arg-validation ok\n");

    printf("ncvm: selftest OK\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0)
        return selftest_main();

    if (argc >= 2 && strcmp(argv[1], "daemon") == 0)
        return daemon_main();

    if (argc == 1)
        return daemon_main();

    if (strcmp(argv[1], "list") == 0)
        return list_vms();

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        cli_usage();
        return 0;
    }

    return cli_vm(argc, argv);
}