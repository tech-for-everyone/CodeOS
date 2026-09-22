#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define MAX_CMD  256
#define MAX_ARGS 16
#define AI_BUF   2048

static void run_cmd(char *line) {
    char *argv[MAX_ARGS];
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= MAX_ARGS) break;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return;

    if (strcmp(argv[0], "exit") == 0) {
        sys_exit(0);
    } else if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) putchar(' ');
            sys_write(argv[i], strlen(argv[i]));
        }
        putchar('\n');
    } else if (strcmp(argv[0], "clear") == 0) {
        sys_write("\033[2J\033[H", 7);
    } else if (strcmp(argv[0], "ai") == 0) {
        if (argc < 2) {
            puts("Usage: ai <question>");
            puts("  Ask FreeCode anything about CodeOS.");
            puts("  Examples:");
            puts("    ai how does the kernel scheduler work");
            puts("    ai what is the network stack");
            puts("    ai explain containers");
            puts("  Press F6 to open the FreeCode overlay.");
        } else {
            char prompt[256] = {0};
            int off = 0;
            for (int i = 1; i < argc && off < 250; i++) {
                if (i > 1) prompt[off++] = ' ';
                for (int j = 0; argv[i][j] && off < 250; j++)
                    prompt[off++] = argv[i][j];
            }
            prompt[off] = 0;
            char resp[AI_BUF];
            int rlen = sys_ai_query(prompt, resp, sizeof(resp));
            if (rlen > 0) {
                resp[rlen] = 0;
                puts(resp);
            } else {
                puts("FreeCode didn't have an answer for that.");
            }
        }
    } else if (strcmp(argv[0], "help") == 0) {
        puts("Commands: echo, clear, help, exit, ai");
        puts("  ai <question>          - Ask FreeCode AI");
        puts("  vm list                - list virtual machines");
        puts("  vm info <name>         - show VM details");
        puts("  vm start <name>        - start a VM");
        puts("  vm stop <name>         - stop a VM");
        puts("  vm pause <name>        - pause a VM");
        puts("  vm resume <name>       - resume a VM");
        puts("  vm run <name>          - run a crosvm VM");
        puts("  vm snapshot <name> <snap> - snapshot a running crosvm VM");
        puts("  vm destroy <name>      - destroy a VM");
        puts("  F6                     - Open FreeCode overlay");
    } else if (strcmp(argv[0], "vm") == 0) {
        if (argc < 2) {
            puts("Usage: vm list|info|start|stop|pause|resume|run|snapshot|destroy");
        } else if (strcmp(argv[1], "list") == 0) {
            char buf[1024];
            int n = sys_vm(VM_CMD_LIST, buf, 0);
            if (n >= 0) {
                buf[sizeof(buf) - 1] = 0;
                sys_write(buf, strlen(buf));
            } else {
                puts("vm: no VMs (or vm manager unavailable)");
            }
        } else if (strcmp(argv[1], "info") == 0) {
            if (argc < 3) { puts("Usage: vm info <name>"); return; }
            char req[512];
            memset(req, 0, sizeof(req));
            strncpy(req, argv[2], 255);
            int n = sys_vm(VM_CMD_INFO, req, 0);
            if (n >= 0) {
                sys_write(req + VM_NAME_MAX, strlen(req + VM_NAME_MAX));
            } else {
                printf("vm: '%s' not found\n", argv[2]);
            }
        } else if (strcmp(argv[1], "start") == 0) {
            if (argc < 3) { puts("Usage: vm start <name>"); return; }
            int r = sys_vm(VM_CMD_START, argv[2], 0);
            printf("vm start: %s\n", r == 0 ? "ok" : "failed");
        } else if (strcmp(argv[1], "stop") == 0) {
            if (argc < 3) { puts("Usage: vm stop <name>"); return; }
            int r = sys_vm(VM_CMD_STOP, argv[2], 0);
            printf("vm stop: %s\n", r == 0 ? "ok" : "failed");
        } else if (strcmp(argv[1], "pause") == 0) {
            if (argc < 3) { puts("Usage: vm pause <name>"); return; }
            int r = sys_vm(VM_CMD_PAUSE, argv[2], 0);
            printf("vm pause: %s\n", r == 0 ? "ok" : "failed");
        } else if (strcmp(argv[1], "resume") == 0) {
            if (argc < 3) { puts("Usage: vm resume <name>"); return; }
            int r = sys_vm(VM_CMD_RESUME, argv[2], 0);
            printf("vm resume: %s\n", r == 0 ? "ok" : "failed");
        } else if (strcmp(argv[1], "run") == 0) {
            if (argc < 3) { puts("Usage: vm run <name>"); return; }
            puts("vm: creating and starting crosvm VM...");
            char cfg[512];
            memset(cfg, 0, sizeof(cfg));
            snprintf(cfg, sizeof(cfg),
                     "%s\0/boot/vmlinux\0/boot/initrd.img\0/var/lib/crosvm/%s/disk.img\0",
                     argv[2], argv[2]);
            /* Struct layout: name[32] kernel[128] initrd[128] rootfs[128] mem u64 cpus int */
            *(uint64_t *)(cfg + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX) = 1024;
            *(int *)(cfg + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX + 8) = 2;
            int r = sys_vm(VM_CMD_CROSVM_CREATE, cfg, 0);
            if (r < 0) {
                printf("vm: crosvm create failed\n");
                return;
            }
            int s = sys_vm(VM_CMD_START, argv[2], 0);
            printf("vm run: %s\n", s == 0 ? "launch staged" : "start failed");
        } else if (strcmp(argv[1], "snapshot") == 0) {
            if (argc < 4) { puts("Usage: vm snapshot <name> <snapshot>"); return; }
            char req[512];
            memset(req, 0, sizeof(req));
            strncpy(req, argv[2], 31);
            strncpy(req + VM_NAME_MAX, argv[3], 31);
            int r = sys_vm(VM_CMD_SNAPSHOT, req, (uint64_t)(uintptr_t)(req + VM_NAME_MAX));
            printf("vm snapshot: %s\n", r == 0 ? "requested" : "failed");
        } else if (strcmp(argv[1], "destroy") == 0) {
            if (argc < 3) { puts("Usage: vm destroy <name>"); return; }
            int r = sys_vm(VM_CMD_DESTROY, argv[2], 0);
            printf("vm destroy: %s\n", r == 0 ? "ok" : "failed");
        } else {
            printf("vm: unknown subcommand '%s'\n", argv[1]);
        }
    } else {
        printf("sh: %s: not found\n", argv[0]);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[MAX_CMD];
    int pos = 0;
    sys_write("$ ", 2);
    while (1) {
        char c;
        int n = sys_read(0, &c, 1);
        if (n <= 0) continue;
        if (c == '\r' || c == '\n') {
            buf[pos] = 0;
            sys_write("\n", 1);
            run_cmd(buf);
            pos = 0;
            sys_write("$ ", 2);
        } else if (c == '\b' || c == 127) {
            if (pos > 0) { pos--; sys_write("\b \b", 3); }
        } else if (c >= ' ' && c < 127 && pos < MAX_CMD - 1) {
            buf[pos++] = c;
            sys_write(&c, 1);
        }
    }
}
