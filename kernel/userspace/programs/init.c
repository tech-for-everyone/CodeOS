#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define MAX_CMD 256
#define MAX_ARGS 16

extern int main(int argc, char **argv);

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
    } else if (strcmp(argv[0], "ls") == 0) {
        const char *path = argc > 1 ? argv[1] : ".";
        int fd = sys_open(path, 0);
        if (fd < 0) { printf("ls: %s: no such file\n", path); return; }
        char buf[512];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            sys_write(buf, n);
        }
    } else if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) { printf("cat: missing file\n"); return; }
        int fd = sys_open(argv[1], 0);
        if (fd < 0) { printf("cat: %s: no such file\n", argv[1]); return; }
        char buf[512];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            sys_write(buf, n);
        }
    } else if (strcmp(argv[0], "clear") == 0) {
        sys_write("\033[2J\033[H", 7);
    } else if (strcmp(argv[0], "help") == 0) {
        puts("Commands: echo, ls, cat, clear, help, exit");
    } else if (strcmp(argv[0], "ps") == 0) {
        puts("init (pid 1)");
    } else {
        printf("init: %s: not found\n", argv[0]);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("DevOS init: starting shell...");
    sys_write("$ ", 2);
    char buf[MAX_CMD];
    int pos = 0;
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
